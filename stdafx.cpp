#include "stdafx.h"

#ifdef _MSC_VER
	#include <stdio.h>
	extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }
#endif