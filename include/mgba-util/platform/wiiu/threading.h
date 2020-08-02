/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef WIIU_THREADING_H
#define WIIU_THREADING_H

#include <mgba-util/common.h>

CXX_GUARD_START

//#include <pthread.h>

#include <coreinit/thread.h>
#include <coreinit/mutex.h>
#include <coreinit/condition.h>
#include <coreinit/semaphore.h>
#include <sys/time.h>
#include <malloc.h>

#define THREAD_ENTRY int

typedef OSThread Thread;
typedef OSThreadEntryPointFn ThreadEntry;
typedef OSMutex Mutex;
typedef OSCondition Condition;

static inline int MutexInit(Mutex* mutex) {
	OSInitMutex(mutex);
	return 0;
}

static inline int MutexDeinit(Mutex* mutex) {
	UNUSED(mutex);
	return 0;
}

static inline int MutexLock(Mutex* mutex) {
	OSLockMutex(mutex);
	return 0;
}

static inline int MutexTryLock(Mutex* mutex) {
	return OSTryLockMutex(mutex);
}

static inline int MutexUnlock(Mutex* mutex) {
	OSUnlockMutex(mutex);
	return 0;
}

static inline int ConditionInit(Condition* cond) {
	OSInitCond(cond);
	return 0;
}

static inline int ConditionDeinit(Condition* cond) {
	UNUSED(cond);
	return 0;
}

static inline int ConditionWait(Condition* cond, Mutex* mutex) {
	OSWaitCond(cond, mutex);
	return 0;
}

static inline int ConditionWaitTimed(Condition* cond, Mutex* mutex, int32_t timeoutMs) {
	UNUSED(timeoutMs); //FIXME
	OSWaitCond(cond, mutex);
	return 0;
}

static inline int ConditionWake(Condition* cond) {
	OSSignalCond(cond);
	return 0;
}

static inline int ThreadCreate(Thread* thread, ThreadEntry entry, void* context) {
	size_t stackSize = 0x8000;
	void* stack = memalign(16, stackSize);
	memset(stack, 0, stackSize);

	OSCreateThread(thread, (void*) entry, 1, context, stack + stackSize, stackSize, 10, OS_THREAD_ATTRIB_AFFINITY_ANY);
	return OSResumeThread(thread);
}

static inline int ThreadJoin(Thread* thread) {
	int result;
	OSJoinThread(thread, &result);
	free(thread->stackStart);
	return result;
}

static inline int ThreadSetName(const char* name) {
	OSSetThreadName(OSGetCurrentThread(), name);
	return 0;
}

CXX_GUARD_END

#endif
