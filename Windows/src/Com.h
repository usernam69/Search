// The COM objects WebView2 hands back are answered by a class with the one
// method it is asked for. Microsoft's WRL gives that class its reference
// count and QueryInterface, so a handler is only its own method.
#pragma once

#include <unknwn.h>
#include <wrl.h>

#include <functional>
#include <utility>

template <typename I, typename... Args>
class FnHandler : public Microsoft::WRL::RuntimeClass<
                      Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, I> {
public:
    explicit FnHandler(std::function<void(Args...)> f) : f_(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        f_(args...);
        return S_OK;
    }

private:
    std::function<void(Args...)> f_;
};

template <typename I, typename... Args>
Microsoft::WRL::ComPtr<I> handler(std::function<void(Args...)> f) {
    return Microsoft::WRL::Make<FnHandler<I, Args...>>(std::move(f));
}
