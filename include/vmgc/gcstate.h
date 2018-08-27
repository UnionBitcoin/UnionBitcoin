#pragma once

#include <list>
#include <vector>
#include <memory>
#include <exception>
#include <string>
#include <inttypes.h>
#include <cstddef>
#include <cstring>
#include "vmgc/exceptions.h"
#include "vmgc/gcobject.h"

namespace vmgc {

// 100MB
#define DEFAULT_MAX_GC_HEAP_SIZE 100*1024*1024

	struct GcObject;

    class GcState {
	private:
		ptrdiff_t _usedsize;
		ptrdiff_t _heapend;
		ptrdiff_t _lastfreepos;
		ptrdiff_t _max_gc_heap_size = DEFAULT_MAX_GC_HEAP_SIZE;
		void* _pstart;
		std::shared_ptr<std::list<std::pair<ptrdiff_t, ptrdiff_t> > > _malloced_buffers; // [ [start_ptr, end_ptr], ... ]
		std::shared_ptr<std::list<ptrdiff_t> > _gc_objects; // list of GcObject managed in GcState

	public:
		// @throws vmgc::GcException
		GcState(ptrdiff_t max_gc_heap_size = DEFAULT_MAX_GC_HEAP_SIZE);
		virtual ~GcState();

		void* gc_malloc(size_t size);
		void gc_free(void* p);
		void gc_free_array(void* p, size_t count, size_t element_size);
		void* gc_realloc(void *p, size_t oldsize, size_t newsize);
		void* gc_malloc_vector(size_t count, size_t element_size);
		// @throws vmgc::GcException
		void* gc_grow_vector(void *p, size_t nelements, size_t* size, size_t element_size, size_t limit);
		size_t gc_objects_count() const;
		std::shared_ptr<std::list<ptrdiff_t> > gc_objects() const;
		void* pstart() const;
		ptrdiff_t usedsize() const;
		void gc_free_all();



		template <typename T>
		T* gc_new_object()
		{
			size_t sz = sizeof(T);
			auto p = gc_malloc(sz);
			if (!p) {
				return nullptr;
			}
			GcObject* obj_p = static_cast<GcObject*>(p);
			new (obj_p)T();
			obj_p->tt = T::type;
			_gc_objects->push_back((intptr_t) p - (intptr_t) _pstart);
			return static_cast<T*>(obj_p);
		}

		template <typename T>
		T* gc_new_object_vector(size_t count)
		{
			if (count <= 0)
				return nullptr;
			size_t sz = sizeof(T);
			std::vector<T*> malloced_items;
			for (size_t i = 0; i < count; i++) {
				auto p = gc_malloc(sz);
				if (!p) {
					for (const auto& item : malloced_items) {
						gc_free(item);
					}
					return nullptr;
				}
				GcObject* obj_p = static_cast<GcObject*>(p);
				new (obj_p)T();
				obj_p->tt = T::type;
				_gc_objects->push_back((intptr_t)p - (intptr_t)_pstart);
				T* t_p = static_cast<T*>(obj_p);
				malloced_items.push_back(t_p);
			}
			return *(malloced_items.begin());
		}



		#define GC_MINSIZEARRAY 4

		template <typename T>
		T* gc_grow_object_vector(T* p, size_t nelements, size_t* size, size_t limit)
		{
			if (nelements + 1 <= *size)
				return p;
			size_t newsize;
			if ((*size) >= limit / 2) {  /* cannot double it? */
				if ((*size) >= limit)  /* cannot grow even a little? */
					throw GcException(std::string("too many objects in gc vector (limit is ") + std::to_string(limit) + ")");
				newsize = limit;  /* still have at least one free place */
			}
			else {
				newsize = (*size) * 2;
				if (newsize < GC_MINSIZEARRAY)
					newsize = GC_MINSIZEARRAY;  /* minimum size */
			}
			T* new_p = gc_new_object_vector<T>(newsize);
			if (!new_p) {
				return nullptr;
			}
			if (p) {
				for (size_t i = 0; i < (*size); i++) {
					*(new_p + i) = *(p + i);
				}
			}
			if (p || (*size) > 0) {
				gc_free_array(p, *size, sizeof(T));
			}
			*size = newsize;
			return new_p;
		}

    };
}