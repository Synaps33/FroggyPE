#include "libco.h"
#include <stdlib.h>

#if defined(__mips__)

#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	jmp_buf context;
	void (*coentry)(void);
	char* stack;
} cothread_context_t;

static cothread_context_t co_primary = {0};
static cothread_context_t *co_creating = NULL;
static cothread_context_t *co_running = NULL;

static void cothread_proc(void)
{
	if (setjmp(co_creating->context) == 1)
	{
		co_running->coentry();
		abort();
	}
}

cothread_t co_active(void)
{
	if (!co_running)
		co_running = &co_primary;
	return (cothread_t)co_running;
}

cothread_t co_create(unsigned int stacksize, void (*coentry)(void))
{
	if (!co_running)
		co_running = &co_primary;

	cothread_context_t *ctx = (cothread_context_t *)malloc(sizeof(cothread_context_t));
	if (!ctx) return NULL;

	ctx->stack = (char*)malloc(stacksize);
	if (!ctx->stack)
	{
		free(ctx);
		return NULL;
	}
	ctx->coentry = coentry;

	co_creating = ctx;

	static void* original_sp;
	static void* modified_sp;
	modified_sp = ctx->stack + stacksize;

	asm volatile (
		"move %0, $sp"
		: "=r" (original_sp)
	);

	asm volatile (
		"move $sp, %0"
		:
		: "r" (modified_sp)
	);

	cothread_proc();

	asm volatile (
		"move $sp, %0"
		:
		: "r" (original_sp)
	);

	return (cothread_t)ctx;
}

void co_delete(cothread_t cothread)
{
	cothread_context_t *ctx = (cothread_context_t *)cothread;
	if (ctx)
	{
		if (ctx->stack)
			free(ctx->stack);
		free(ctx);
	}
}

void co_switch(cothread_t cothread)
{
	if (setjmp(co_running->context) == 0)
	{
		co_running = (cothread_context_t *)cothread;
		longjmp(co_running->context, 1);
	}
}

#ifdef __cplusplus
}
#endif

#else // Non-MIPS (Host PC / Linux x86_64)

#include <ucontext.h>

#ifdef __cplusplus
extern "C" {
#endif

static ucontext_t co_primary;
static ucontext_t *co_running = 0;

cothread_t co_active(void)
{
   if (!co_running)
      co_running = &co_primary;
   return (cothread_t)co_running;
}

cothread_t co_create(unsigned int heapsize, void (*coentry)(void))
{
   ucontext_t *thread;
   if (!co_running)
      co_running = &co_primary;

   if ((thread = (ucontext_t*)malloc(sizeof(ucontext_t))))
   {
      if ((!getcontext(thread) && !(thread->uc_stack.ss_sp = 0)) && (thread->uc_stack.ss_sp = malloc(heapsize)))
      {
         thread->uc_link = co_running;
         thread->uc_stack.ss_size = heapsize;
         makecontext(thread, coentry, 0);
      }
      else
      {
         co_delete((cothread_t)thread);
         thread = 0;
      }
   }
   return (cothread_t)thread;
}

void co_delete(cothread_t cothread)
{
   if (!cothread)
      return;

   if (((ucontext_t*)cothread)->uc_stack.ss_sp)
      free(((ucontext_t*)cothread)->uc_stack.ss_sp);
   free(cothread);
}

void co_switch(cothread_t cothread)
{
   ucontext_t *old_thread = co_running;
   co_running             = (ucontext_t*)cothread;
   swapcontext(old_thread, co_running);
}

#ifdef __cplusplus
}
#endif

#endif
