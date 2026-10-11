#ifndef TB_JSMINI_H
#define TB_JSMINI_H

#include "node.h"

Node* js_compile_script(const char* src, char* err, size_t errlen);
Node* js_compile_expr(const char* src, char* err, size_t errlen);

#endif
