/**
 * @file single_call.hpp
 * @author WangJun
 * @brief 工具函数，确保同时只有一个线程调用。其他线程立即返回，不阻塞等待也不重试。
 * 当然，没法阻止外部重试。
 * @version 0.1
 * @date 2021/8/16 18:27
 */
#ifndef _SYNC_SINGLE_CALL_HPP_
#define _SYNC_SINGLE_CALL_HPP_
#if (defined __GNUC__ && ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)) || defined _MSC_VER
#pragma once
#endif /* __GNUC__ >= 3.4 || _MSC_VER */

#include <atomic>
#include <functional>
#include "Defer.hpp"

namespace juliet::sync {

/**
 * 同时只有一个调用成功
 * @tparam _Callable 要调用的函数对象
 * @tparam _Args 要调用的函数参数
 * @param __doCall 外部提供一个标识。默认为false，代表没有任何线程调用callable
 * @param __f 函数对象
 * @param __args 参数列表
 * @return 是否调用
 */
template<typename _Callable, typename... _Args>
bool SingleCall(std::atomic_bool& __doCall, _Callable&& __f, _Args&&... __args) {
    bool expected = false;
    if (!__doCall.compare_exchange_strong(expected, true))
        return false;
    // 防止未捕获的异常造成标识没有重置。
    DEFER([&__doCall]() { __doCall = false; });
    // 如果call可能会引发异常，外部还是需要自行捕获。
    auto call = std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);
    call();
    return true;
}

} // namespace sync

#endif // !_SYNC_SINGLE_CALL_HPP_
