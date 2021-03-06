/* -*- Mode: c++; c-basic-offset: 4; tab-width: 40; indent-tabs-mode: nil -*- */
/* vim: set ts=40 sw=4 et tw=99: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Mozilla SpiderMonkey property tree implementation
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2002-2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brendan Eich <brendan@mozilla.org>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <new>

#include "jstypes.h"
#include "jsprf.h"
#include "jsapi.h"
#include "jscntxt.h"
#include "jsgc.h"
#include "jspropertytree.h"
#include "jsscope.h"

#include "jsobjinlines.h"
#include "jsscopeinlines.h"

using namespace js;

inline HashNumber
ShapeHasher::hash(const Lookup &l)
{
    return l.hash();
}

inline bool
ShapeHasher::match(const Key k, const Lookup &l)
{
    return k->matches(l);
}

Shape *
PropertyTree::newShape(JSContext *cx)
{
    Shape *shape = js_NewGCShape(cx);
    if (!shape) {
        JS_ReportOutOfMemory(cx);
        return NULL;
    }
    return shape;
}

static KidsHash *
HashChildren(Shape *kid1, Shape *kid2)
{
    KidsHash *hash = OffTheBooks::new_<KidsHash>();
    if (!hash || !hash->init(2)) {
        Foreground::delete_(hash);
        return NULL;
    }

    JS_ALWAYS_TRUE(hash->putNew(kid1, kid1));
    JS_ALWAYS_TRUE(hash->putNew(kid2, kid2));
    return hash;
}

bool
PropertyTree::insertChild(JSContext *cx, Shape *parent, Shape *child)
{
    JS_ASSERT(!parent->inDictionary());
    JS_ASSERT(!child->parent);
    JS_ASSERT(!child->inDictionary());
    JS_ASSERT(cx->compartment == compartment);
    JS_ASSERT(child->compartment() == parent->compartment());

    KidsPointer *kidp = &parent->kids;

    if (kidp->isNull()) {
        child->setParent(parent);
        kidp->setShape(child);
        return true;
    }

    if (kidp->isShape()) {
        Shape *shape = kidp->toShape();
        JS_ASSERT(shape != child);
        JS_ASSERT(!shape->matches(child));

        KidsHash *hash = HashChildren(shape, child);
        if (!hash) {
            JS_ReportOutOfMemory(cx);
            return false;
        }
        kidp->setHash(hash);
        child->setParent(parent);
        return true;
    }

    if (!kidp->toHash()->putNew(child, child)) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    child->setParent(parent);
    return true;
}

void
Shape::removeChild(Shape *child)
{
    JS_ASSERT(!child->inDictionary());

    KidsPointer *kidp = &kids;

    if (kidp->isShape()) {
        JS_ASSERT(kidp->toShape() == child);
        kidp->setNull();
        return;
    }

    KidsHash *hash = kidp->toHash();
    JS_ASSERT(hash->count() >= 2);      /* otherwise kidp->isShape() should be true */

    hash->remove(child);

    if (hash->count() == 1) {
        /* Convert from HASH form back to SHAPE form. */
        KidsHash::Range r = hash->all();
        Shape *otherChild = r.front();
        JS_ASSERT((r.popFront(), r.empty()));    /* No more elements! */
        kidp->setShape(otherChild);
        js::UnwantedForeground::delete_(hash);
    }
}

/*
 * We need a read barrier for the shape tree, since these are weak pointers.
 */
static Shape *
ReadBarrier(Shape *shape)
{
#ifdef JSGC_INCREMENTAL
    JSCompartment *comp = shape->compartment();
    if (comp->needsBarrier()) {
        Shape *tmp = shape;
        MarkShapeUnbarriered(comp->barrierTracer(), &tmp, "read barrier");
        JS_ASSERT(tmp == shape);
    }
#endif
    return shape;
}

Shape *
PropertyTree::getChild(JSContext *cx, Shape *parent, uint32_t nfixed, const StackShape &child)
{
    Shape *shape;

    JS_ASSERT(parent);

    /*
     * The property tree has extremely low fan-out below its root in
     * popular embeddings with real-world workloads. Patterns such as
     * defining closures that capture a constructor's environment as
     * getters or setters on the new object that is passed in as
     * |this| can significantly increase fan-out below the property
     * tree root -- see bug 335700 for details.
     */
    KidsPointer *kidp = &parent->kids;
    if (kidp->isShape()) {
        shape = kidp->toShape();
        if (shape->matches(child))
            return ReadBarrier(shape);
    } else if (kidp->isHash()) {
        shape = *kidp->toHash()->lookup(child);
        if (shape)
            return ReadBarrier(shape);
    } else {
        /* If kidp->isNull(), we always insert. */
    }

    RootStackShape childRoot(cx, &child);
    RootShape parentRoot(cx, &parent);

    shape = newShape(cx);
    if (!shape)
        return NULL;

    new (shape) Shape(child, nfixed);

    if (!insertChild(cx, parent, shape))
        return NULL;

    return shape;
}

void
Shape::finalize(JSContext *cx, bool background)
{
    if (!inDictionary()) {
        if (parent && parent->isMarked())
            parent->removeChild(this);

        if (kids.isHash())
            cx->delete_(kids.toHash());
    }
}

#ifdef DEBUG

void
KidsPointer::checkConsistency(const Shape *aKid) const
{
    if (isShape()) {
        JS_ASSERT(toShape() == aKid);
    } else {
        JS_ASSERT(isHash());
        KidsHash *hash = toHash();
        KidsHash::Ptr ptr = hash->lookup(aKid);
        JS_ASSERT(*ptr == aKid);
    }
}

void
Shape::dump(JSContext *cx, FILE *fp) const
{
    jsid propid = this->propid();

    JS_ASSERT(!JSID_IS_VOID(propid));

    if (JSID_IS_INT(propid)) {
        fprintf(fp, "[%ld]", (long) JSID_TO_INT(propid));
    } else if (JSID_IS_DEFAULT_XML_NAMESPACE(propid)) {
        fprintf(fp, "<default XML namespace>");
    } else {
        JSLinearString *str;
        if (JSID_IS_ATOM(propid)) {
            str = JSID_TO_ATOM(propid);
        } else {
            JS_ASSERT(JSID_IS_OBJECT(propid));
            JSString *s = ToStringSlow(cx, IdToValue(propid));
            fputs("object ", fp);
            str = s ? s->ensureLinear(cx) : NULL;
        }
        if (!str)
            fputs("<error>", fp);
        else
            FileEscapedString(fp, str, '"');
    }

    fprintf(fp, " g/s %p/%p slot %d attrs %x ",
            JS_FUNC_TO_DATA_PTR(void *, base()->rawGetter),
            JS_FUNC_TO_DATA_PTR(void *, base()->rawSetter),
            hasSlot() ? slot() : -1, attrs);

    if (attrs) {
        int first = 1;
        fputs("(", fp);
#define DUMP_ATTR(name, display) if (attrs & JSPROP_##name) fputs(&(" " #display)[first], fp), first = 0
        DUMP_ATTR(ENUMERATE, enumerate);
        DUMP_ATTR(READONLY, readonly);
        DUMP_ATTR(PERMANENT, permanent);
        DUMP_ATTR(GETTER, getter);
        DUMP_ATTR(SETTER, setter);
        DUMP_ATTR(SHARED, shared);
#undef  DUMP_ATTR
        fputs(") ", fp);
    }

    fprintf(fp, "flags %x ", flags);
    if (flags) {
        int first = 1;
        fputs("(", fp);
#define DUMP_FLAG(name, display) if (flags & name) fputs(&(" " #display)[first], fp), first = 0
        DUMP_FLAG(HAS_SHORTID, has_shortid);
        DUMP_FLAG(IN_DICTIONARY, in_dictionary);
#undef  DUMP_FLAG
        fputs(") ", fp);
    }

    fprintf(fp, "shortid %d\n", shortid());
}

void
Shape::dumpSubtree(JSContext *cx, int level, FILE *fp) const
{
    if (!parent) {
        JS_ASSERT(level == 0);
        JS_ASSERT(JSID_IS_EMPTY(propid_));
        fprintf(fp, "class %s emptyShape\n", getObjectClass()->name);
    } else {
        fprintf(fp, "%*sid ", level, "");
        dump(cx, fp);
    }

    if (!kids.isNull()) {
        ++level;
        if (kids.isShape()) {
            Shape *kid = kids.toShape();
            JS_ASSERT(kid->parent == this);
            kid->dumpSubtree(cx, level, fp);
        } else {
            const KidsHash &hash = *kids.toHash();
            for (KidsHash::Range range = hash.all(); !range.empty(); range.popFront()) {
                Shape *kid = range.front();

                JS_ASSERT(kid->parent == this);
                kid->dumpSubtree(cx, level, fp);
            }
        }
    }
}

void
js::PropertyTree::dumpShapes(JSContext *cx)
{
    static bool init = false;
    static FILE *dumpfp = NULL;
    if (!init) {
        init = true;
        const char *name = getenv("JS_DUMP_SHAPES_FILE");
        if (!name)
            return;
        dumpfp = fopen(name, "a");
    }

    if (!dumpfp)
        return;

    JSRuntime *rt = cx->runtime;
    fprintf(dumpfp, "rt->gcNumber = %lu", (unsigned long)rt->gcNumber);

    for (CompartmentsIter c(rt); !c.done(); c.next()) {
        if (rt->gcCurrentCompartment != NULL && rt->gcCurrentCompartment != c)
            continue;

        fprintf(dumpfp, "*** Compartment %p ***\n", (void *)c.get());

        /*
        typedef JSCompartment::EmptyShapeSet HS;
        HS &h = c->emptyShapes;
        for (HS::Range r = h.all(); !r.empty(); r.popFront()) {
            Shape *empty = r.front();
            empty->dumpSubtree(cx, 0, dumpfp);
            putc('\n', dumpfp);
        }
        */
    }
}
#endif
