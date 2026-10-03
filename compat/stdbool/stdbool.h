#ifndef _BS_STDBOOL_H_
#define _BS_STDBOOL_H_

#if defined(__cplusplus) && (!defined(_MSC_VER) || _MSC_VER >= 1100)

#define bool  bool
#define true  true
#define false false

#else

#define bool  char
#define true  1
#define false 0

#endif

#define __bool_true_false_are_defined 1

#endif /* _BS_STDBOOL_H_ */
