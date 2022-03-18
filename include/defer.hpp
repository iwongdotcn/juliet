#ifndef _JULIET_DEFER_HPP_
#define _JULIET_DEFER_HPP_

#if (defined __GNUC__ && ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)) || defined _MSC_VER
#pragma once
#endif /* __GNUC__ >= 3.4 || _MSC_VER */

#include <functional>

class DeferGuard
{
public:
    template<typename _Callable, typename... _Args>
    DeferGuard(_Callable&& callOnExit, _Args&&... args)
    : routine_(std::bind(std::forward<_Callable>(callOnExit), std::forward<_Args>(args)...)) {

    }

    ~DeferGuard() {
        if (routine_)
            routine_();
    }

    void Cancel() {
        routine_ = nullptr;
    }

private:
    // no default constructor
    DeferGuard() = delete;
    // noncopyable
    DeferGuard(const DeferGuard&) = delete;
    void operator =(const DeferGuard&) = delete;

    std::function<void()> routine_;
};

#define _DEFERGUARD_CATFOUNR(a, b, c, s) a##s##b##s##c
#define _DEFERGUARD_MAKENAME(prefix,infix,suffix) _DEFERGUARD_CATFOUNR(prefix,infix,suffix,_)

#define DEFER(routine) \
DeferGuard _DEFERGUARD_MAKENAME(__DeferGuard,__LINE__,__COUNTER__)(routine)

#define ON_SCOPE_EXIT(routine)  DEFER(routine)

#endif /* _JULIET_DEFER_HPP_ */
