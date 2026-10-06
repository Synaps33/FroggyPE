#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void* cothread_t;

cothread_t co_active(void);
cothread_t co_create(unsigned int stacksize, void (*coentry)(void));
void co_delete(cothread_t cothread);
void co_switch(cothread_t cothread);

#ifdef __cplusplus
}
#endif
