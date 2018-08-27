#pragma once
#include <vector>

namespace vmgc {

	typedef unsigned char gc_type;

	// all gc objects managed should extend GcObject
	struct GcObject {
		const static vmgc::gc_type type = 0;
		gc_type tt = 0; // gc object type

		GcObject() {}
		virtual ~GcObject() {}
	};
    
}