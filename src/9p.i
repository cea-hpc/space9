%module space9
%{
#include <mooshika.h>
#include "9p.h"
%}

%newobject p9newhandle;
%inline %{
  struct p9_handle *p9newhandle(char *conf) {
    struct p9_handle *temp;
    p9_init(&temp, conf);
    return temp;
  }
%}

%newobject p9destroyhandle;
%inline %{
  void p9destroyhandle(struct p9_handle *handle) {
    p9_destroy(&handle);
 }
%}

%include "../include/9p.h"
