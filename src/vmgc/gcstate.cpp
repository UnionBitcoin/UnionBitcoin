#include "vmgc/gcstate.h"
#include "vmgc/gcobject.h"
#include <algorithm>

namespace vmgc {
	GcState::GcState(ptrdiff_t max_gc_heap_size) {
		this->_malloced_buffers = std::make_shared<std::list<std::pair<ptrdiff_t, ptrdiff_t> > >();
		this->_gc_objects = std::make_shared<std::list<ptrdiff_t> >();
		this->_max_gc_heap_size = max_gc_heap_size;
		_usedsize = 0;
		_heapend = 0;
		_pstart = nullptr;
		_lastfreepos = 0;

		_pstart = malloc(this->_max_gc_heap_size);
		_heapend = this->_max_gc_heap_size;
		if (!_pstart) {
			throw GcException("malloc gc heap error");
		}
	}

	GcState::~GcState() {
		if (_pstart) {
			gc_free_all();
			free(_pstart);
			_pstart = nullptr;
			_usedsize = 0;
			_heapend = 0;
		}
	}

	static size_t align8(size_t s) {
		if ((s & 0x7) == 0)
			return s;
		return ((s >> 3) + 1) << 3;
	}

	void* GcState::gc_malloc(size_t size) {
		if (size <= 0)
			return nullptr;
		size = align8(size);
		if (size > _heapend)
		{
			return nullptr;
		}

		if (_pstart == nullptr) {
			return nullptr;
		}

		if ((_usedsize + size) > _heapend) {
			return nullptr;
		}

		void *p = nullptr;
		if (_malloced_buffers->empty())
		{
			p = _pstart;
			_usedsize = _usedsize + size;
			_malloced_buffers->push_back(std::make_pair(0, size));
			_lastfreepos = (ptrdiff_t)size;
			return p;
		}

		//try malloc from last pos
		if (_lastfreepos + (ptrdiff_t)size <= _heapend)
		{
			p = (void *)((intptr_t)(_pstart) + _lastfreepos);
			_usedsize = _usedsize + size;
			_malloced_buffers->push_back(std::make_pair(_lastfreepos, size));
			_lastfreepos = _lastfreepos + (ptrdiff_t)size;
			return p;
		}


		//try find space among mems 
		std::pair<ptrdiff_t, ptrdiff_t> last_pair;
		auto begin = _malloced_buffers->begin();
		for (auto it = begin; it != _malloced_buffers->end(); ++it)
		{
			if (it == begin)
			{
				if (it->first > (ptrdiff_t)size)
				{
					// can alloc memory before first block
					ptrdiff_t offset = 0;
					void *p = _pstart;
					_malloced_buffers->insert(_malloced_buffers->begin(), std::make_pair(offset, size));
					_usedsize = _usedsize + (ptrdiff_t)size;
					return p;
				}
				last_pair = *it;
				continue;
			}
			if (it->first > last_pair.first + last_pair.second + (ptrdiff_t)size)
			{
				ptrdiff_t offset = last_pair.first + last_pair.second;
				p = (void*)((intptr_t)_pstart + offset);
				_malloced_buffers->insert(it, std::make_pair(offset, size));
				_usedsize = _usedsize + (ptrdiff_t)size;
				return p;
			}
			last_pair = *it;
		}

		// no space   
		// TODO: compact
		return nullptr;
	}
	void GcState::gc_free(void* p) {
		if (nullptr == p)
			return;
		auto offset = (intptr_t)p - (intptr_t)(_pstart);
		if (offset < 0 || offset >= _heapend)
			return;

		std::pair<ptrdiff_t, ptrdiff_t> last_pair;
		last_pair.first = 0;
		last_pair.second = 0;
		auto begin = _malloced_buffers->begin();
		for (auto it = begin; it != _malloced_buffers->end(); ++it)
		{
			if (it->first == (ptrdiff_t)offset)
			{
				ptrdiff_t size = it->second;
				_malloced_buffers->erase(it);
				// free GcObject
				auto found_gc_object = std::find(_gc_objects->begin(), _gc_objects->end(), offset);
				if (found_gc_object != _gc_objects->end()) {
					auto gc_obj = (GcObject*)p ;
					gc_obj->~GcObject();
					_gc_objects->erase(found_gc_object);
				}
				_usedsize = _usedsize - (ptrdiff_t)size;

				if ((offset + size) == _lastfreepos) {
					_lastfreepos = (ptrdiff_t)(last_pair.first + last_pair.second);
				}
				return;
			}
			last_pair = *it;
		}
	}

	void GcState::gc_free_array(void* p, size_t count, size_t size)
	{
		if (!p || count <= 0)
			return;
		for (size_t i = 0; i < count; i++) {
			gc_free((void*)((intptr_t)p + i*size));
		}
	}

	void* GcState::gc_realloc(void *p, size_t oldsz, size_t newsz) {
		if (_pstart == nullptr) return nullptr;

		if ((oldsz < 0) || (newsz < 0))return nullptr;

		if (newsz == 0) {
			//do some free
			if (nullptr != p) {
				gc_free(p);
			}
			return nullptr;
		}

		void *newp = nullptr;
		if (p == nullptr)   // alloc new
		{
			newp = gc_malloc(newsz);
			return newp;
		}
		else
		{
			if (newsz <= oldsz) {
				//no op
				return p;
			}
			newp = gc_malloc(newsz);
			if (newp == nullptr)return nullptr;
			//copy data
			memcpy(newp, p, oldsz * sizeof(ptrdiff_t));
			//free old 
			gc_free(p);
			return newp;
		}
	}

	void* GcState::gc_malloc_vector(size_t count, size_t size)
	{
		if (count <= 0)
			return nullptr;
		return gc_malloc(count * size);
	}

	void* GcState::gc_grow_vector(void *p, size_t nelements, size_t* size, size_t element_size, size_t limit)
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
		auto new_p = gc_malloc_vector(newsize, element_size);
		if (!new_p) {
			return nullptr;
		}
		if (p) {
			memcpy(new_p, p, (*size) * element_size);
		}
		if (p || (*size) > 0) {
			gc_free_array(p, *size, element_size);
		}
		*size = newsize;
		return new_p;
	}

	size_t GcState::gc_objects_count() const
	{
		return _gc_objects->size();
	}

	std::shared_ptr<std::list<ptrdiff_t> > GcState::gc_objects() const
	{
		return _gc_objects;
	}

	void* GcState::pstart() const {
		return _pstart;
	}

	ptrdiff_t GcState::usedsize() const {
		return _usedsize;
	}

	void GcState::gc_free_all()
	{
		if (_pstart) {
			for (const auto& offset : *_gc_objects) {
				auto p = (GcObject*)((intptr_t)_pstart + offset);
				p->~GcObject();
			}
			_gc_objects->clear();
			_usedsize = 0;
			_lastfreepos = 0;
		}
	}

}