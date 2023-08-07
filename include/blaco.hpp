#pragma once

#include <functional>
#include <thread>

#include "blconfig.hpp"
#include "SockAddr.hpp"
#include "blaio.h"
#include "os/windows/blaco_impl2.hpp"

namespace bl {

#define BLACO_AWAIT_HINT nodiscard("Did you forget to co_await?")

    template<bool suspend>
    struct suspend_ {
        constexpr bool await_ready() const noexcept {
            return !suspend;
        }
        constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}
    };

    template<typename Task, bool suspend_initial>
    struct PromiseBase {
        auto initial_suspend() noexcept { return suspend_<suspend_initial>{}; }
        void unhandled_exception() noexcept {}

        auto final_suspend() const noexcept {
            struct awaiter {
                bool await_ready() const noexcept { return false; }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<typename Task::promise_type> h) noexcept {
                    auto waiter = h.promise().waiter_;
                    return waiter ? waiter : std::noop_coroutine();
                }

                void await_resume() const noexcept {}
            };
            return awaiter{};
        }

        std::coroutine_handle<> waiter_;
    };

    /*
    * @brief Coroutine
    * @param T type of co_return
    * @param suspend_initial false-don't suspend at initial, true-suspend
    */
    template<typename T, bool suspend_initial = true>
    struct task {
        struct promise_type : public PromiseBase<task, suspend_initial> {
            task get_return_object() noexcept { return { std::coroutine_handle<task::promise_type>::from_promise(*this) }; }
            void return_value(T r) noexcept { v_ = r; }
            T v_;
        };

        auto get_handle() const noexcept { return handle_; }

        bool await_ready() const noexcept { return handle_.done(); }
        T await_resume() const noexcept { return std::move(handle_.promise().v_); }
        auto await_suspend(std::coroutine_handle<> h) { handle_.promise().waiter_ = h; }

        std::coroutine_handle<task::promise_type> handle_;
    };

    template<bool suspend_initial>
    struct task<void, suspend_initial> {
        struct promise_type : PromiseBase<task, suspend_initial> {
            task get_return_object() noexcept { return { std::coroutine_handle<task::promise_type>::from_promise(*this) }; }
            void return_void() noexcept {}
        };

        auto get_handle() const noexcept { return handle_; }

        bool await_ready() const noexcept { return handle_.done(); }
        void await_resume() const noexcept {}
        auto await_suspend(std::coroutine_handle<> h) noexcept { handle_.promise().waiter_ = h; }

        std::coroutine_handle<promise_type> handle_;
    };

    static void _onGoTask(void* coroAddr) {
        auto coro = std::coroutine_handle<>::from_address(coroAddr);
        coro.resume();
    }

    /*
    * @brief schedule coroutine to run in thread pool
    * @param[in] h coroutine handle
    * @param[in] isIoTask true-should be scheduled to run in io worker threads, false-other
    */
    inline void go(std::coroutine_handle<> h, bool isIoTask = true) {
        BlPostTask(_onGoTask, h.address(), isIoTask);
    }

    template<bool isIoTask>
    struct GoAwaiter {
        bool await_ready() const noexcept { return false; }
        void await_resume() const noexcept {}
        auto await_suspend(std::coroutine_handle<> h) noexcept { go(h, isIoTask); }
    };

    /*
    * @brief schedule current coroutine to run in the io worker threads
    * @return awaiter{}
    */
    inline GoAwaiter<true> goIo() { return {}; }

    /*
    * @brief schedule current coroutine to run in other worker threads
    * @return awaiter{}
    */
    inline GoAwaiter<false> goOther() { return {}; }

    /*
    * @brief Get local address part of a socket
    * @param[in] sock
    * @param[out] addr
    * @return
    *   @retval <>0 failed, call BlGetLastError() for error code
    *   @retval =0 ok
    */
    inline int SockGetLocalAddr(SOCKET sock, SockAddr& addr) {
        socklen_t addrLen;
        struct sockaddr* addr1 = addr.get_sockaddr(&addrLen);
        return BlSockGetLocalAddr(sock, addr1, &addrLen);
    }

    /*
    * @brief Get peer(remote) address part of a socket
    * @param[in] sock
    * @param[out] addr
    * @return
    *   @retval <>0 failed, call BlGetLastError() for error code
    *   @retval =0 ok
    */
    inline int SockGetPeerAddr(SOCKET sock, SockAddr& addr) {
        socklen_t addrLen;
        struct sockaddr* addr1 = addr.get_sockaddr(&addrLen);
        return BlSockGetPeerAddr(sock, addr1, &addrLen);
    }

    template<typename BaseT, typename RetT>
    struct LazyIoBase : public BaseT {
        bool await_ready() { return false; }
        RetT await_resume() { return this->base.ret; }
        static void onCompleted(void* coro) {
            std::coroutine_handle<>::from_address(coro).resume();
        }
        void set_resume_point(std::coroutine_handle<> current) {
            this->base.coro = current.address();
        }
    };

    struct LazyAccept : LazyIoBase<BlTcpAccept_t, int> {
        LazyAccept(SOCKET sock, sa_family_t family=AF_INET, SockAddr* peer=nullptr) {
            peerLen = sizeof(peer->addr);
            BlInitTcpAccept(this, sock, family, peer? peer->get_sockaddr(): nullptr, &peerLen, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoTcpAccept(this);
        }

        socklen_t peerLen;
    };

    struct LazyConnect : LazyIoBase<BlTcpConnect_t, int> {
        LazyConnect(SOCKET sock, const SockAddr& addr) {
            BlInitTcpConnect(this, sock, addr.get_sockaddr(nullptr), onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoTcpConnect(this);
        }
    };

    struct LazySend : LazyIoBase<BlSockSend_t, int> {
        LazySend(SOCKET sock, const void* buf, uint32_t len, int flags) {
            BlInitSockSend(this, sock, buf, len, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockSend(this);
        }
    };

    struct LazySendVec : LazyIoBase<BlSockSendVec_t, int> {
        LazySendVec(SOCKET sock, const iovec* bufVec, size_t bufCnt, int flags) {
            BlInitSockSendVec(this, sock, bufVec, bufCnt, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockSendVec(this);
        }
    };

    struct LazySendTo : LazyIoBase<BlSockSendTo_t, int> {
        LazySendTo(SOCKET sock, const SockAddr& addr, const void* buf, uint32_t len, int flags) {
            BlInitSockSendTo(this, sock, addr.get_sockaddr(), buf, len, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockSendTo(this);
        }
    };

    struct LazySendVecTo : LazyIoBase<BlSockSendVecTo_t, int> {
        LazySendVecTo(SOCKET sock, const SockAddr& addr,
            const iovec* bufVec, size_t bufCnt, int flags) {
            BlInitSockSendVecTo(this, sock, addr.get_sockaddr(), bufVec, bufCnt, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockSendVecTo(this);
        }
    };

    struct LazyMustSend : LazyIoBase<BlSockMustSend_t, int> {
        LazyMustSend(SOCKET sock, const void* buf, uint32_t len, int flags) {
            BlInitSockMustSend(this, sock, buf, len, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockMustSend(this);
        }
    };

    struct LazyMustSendVec : LazyIoBase<BlSockMustSendVec_t, int> {
        LazyMustSendVec(SOCKET sock, const iovec* bufVec, size_t bufCnt, int flags) {
            BlInitSockMustSendVec(this, sock, bufVec, bufCnt, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockMustSendVec(this);
        }
    };

    struct LazyRecv : LazyIoBase<BlSockRecv_t, int> {
        LazyRecv(SOCKET sock, void* buf, uint32_t len, int flags) {
            BlInitSockRecv(this, sock, buf, len, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockRecv(this);
        }
    };

    struct LazyRecvVec : LazyIoBase<BlSockRecvVec_t, int> {
        LazyRecvVec(SOCKET sock, iovec* bufVec, size_t bufCnt, int flags) {
            BlInitSockRecvVec(this, sock, bufVec, bufCnt, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockRecvVec(this);
        }
    };

    struct LazyRecvFrom : LazyIoBase<BlSockRecvFrom_t, int> {
        LazyRecvFrom(SOCKET sock, SockAddr& addr, void* buf, uint32_t len, int flags) {
            addrLen = sizeof(addr.addr);
            BlInitSockRecvFrom(this, sock, addr.get_sockaddr(), &addrLen,
                buf, len, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockRecvFrom(this);
        }

        socklen_t addrLen;
    };

    struct LazyRecvVecFrom : LazyIoBase<BlSockRecvVecFrom_t, int> {
        LazyRecvVecFrom(SOCKET sock, SockAddr& addr, iovec* bufVec, size_t bufCnt, int flags) {
            addrLen = sizeof(addr.addr);
            BlInitSockRecvVecFrom(this, sock, addr.get_sockaddr(), &addrLen,
                bufVec, bufCnt, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockRecvVecFrom(this);
        }

        socklen_t addrLen;
    };

    struct LazyMustRecv : LazyIoBase<BlSockMustRecv_t, int> {
        LazyMustRecv(SOCKET sock, void* buf, uint32_t len, int flags) {
            BlInitSockMustRecv(this, sock, buf, len, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockMustRecv(this);
        }
    };

    struct LazyMustRecvVec : LazyIoBase<BlSockMustRecvVec_t, int> {
        LazyMustRecvVec(SOCKET sock, iovec* bufVec, size_t bufCnt, int flags) {
            BlInitSockMustRecvVec(this, sock, bufVec, bufCnt, flags, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoSockMustRecvVec(this);
        }
    };

    struct LazyTcpClose : LazyIoBase<BlTcpClose_t, int> {
        LazyTcpClose(SOCKET sock) {
            BlInitTcpClose(this, sock, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoTcpClose(this);
        }
    };

    struct LazyTcpShutdown : LazyIoBase<BlTcpShutdown_t, int> {
        LazyTcpShutdown(SOCKET sock, int how) {
            BlInitTcpShutdown(this, sock, how, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoTcpShutdown(this);
        }
    };

    /*
    * @brief Connect to TCP server
    * @param[in] sock a TCP socket
    * @param[in] addr server address(ipv4 or ipv6)
    * @return awaiter{int ret}
    *   @retval ret=0, ok
    *   @retval ret<0, -ret is the error code
    */
    [[BLACO_AWAIT_HINT]]
    inline auto TcpConnect(SOCKET sock, const SockAddr& addr) {
        return LazyConnect(sock, addr);
    }

    /*
    * @brief Accept from a TCP client
    * @param[in] sock a TCP socket
    * @param[in] family address family of `sock`
    * @param[out] peer nullptr-don't want, other-will be filled with the address of peer
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the accepted socket, `peer` was filled.
    *   @retval ret<0, -ret is the error code, *peer not modified.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto TcpAccept(SOCKET sock, sa_family_t family, SockAddr* peer) noexcept {
        return LazyAccept(sock, family, peer);
    }

    /*
    * @brief Send data using socket
    * @param[in] sock a socket
    * @param[in] buf
    * @param[in] n number of data bytes to send
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockSend(SOCKET sock, const void* buf, uint32_t n, int flags = 0) noexcept {
        return LazySend(sock, buf, n, flags);
    }

    /*
    * @brief Send data vector using socket
    * @param[in] sock a socket
    * @param[in] bufVec
    * @param[in] bufCnt number of buffers in bufVec to send
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    * @note bufCnt should be >0 and <= MAX_BUFS_IN_IOVEC(8), else get an EINVAL err
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockSendVec(SOCKET sock, const iovec* bufVec, size_t bufCnt, int flags = 0) noexcept {
        return LazySendVec(sock, bufVec, bufCnt, flags);
    }

    /*
    * @brief Send data to an address using socket
    * @param[in] sock a socket
    * @param[in] address to send
    * @param[in] buf
    * @param[in] n number of data bytes to send
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockSendTo(SOCKET sock, const SockAddr& addr, const void* buf, uint32_t n, int flags = 0) noexcept {
        return LazySendTo(sock, addr, buf, n, flags);
    }

    /*
    * @brief Send data vector to an address using socket
    * @param[in] sock a socket
    * @param[in] address to send
    * @param[in] bufVec
    * @param[in] bufCnt number of buffers in bufVec to send
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    * @note bufCnt should be >0 and <= MAX_BUFS_IN_IOVEC(8), else get an EINVAL err
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockSendVecTo(SOCKET sock, const SockAddr& addr, const iovec* bufVec, size_t bufCnt, int flags = 0) noexcept {
        return LazySendVecTo(sock, addr, bufVec, bufCnt, flags);
    }

    /*
    * @brief Receive data using socket
    * @param[in] sock a socket
    * @param[in] buf
    * @param[in] n number of data bytes to receive
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockRecv(SOCKET sock, void* buf, uint32_t n, int flags = 0) noexcept {
        return LazyRecv(sock, buf, n, flags);
    }

    /*
    * @brief Receive data vector using socket
    * @param[in] sock a socket
    * @param[in] bufVec
    * @param[in] bufCnt number of buffers in bufVec to receive
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    * @note bufCnt should be >0 and <= MAX_BUFS_IN_IOVEC(8), else get an EINVAL err
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockRecvVec(SOCKET sock, iovec* bufVec, size_t bufCnt, int flags = 0) noexcept {
        return LazyRecvVec(sock, bufVec, bufCnt, flags);
    }

    /*
    * @brief Receive data from an address using socket
    * @param[in] sock a socket
    * @param[out] nullptr - don't need the address, else - get the address that is the source of the data
    * @param[in] buf
    * @param[in] n number of data bytes to receive
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockRecvFrom(SOCKET sock, SockAddr& addr, char* buf, uint32_t n, int flags = 0) noexcept {
        return LazyRecvFrom(sock, addr, buf, n, flags);
    }

    /*
    * @brief Receive data vector from an address using socket
    * @param[in] sock a socket
    * @param[out] addr nullptr - don't need the address, else - get the address that is the source of the data
    * @param[in] bufVec
    * @param[in] bufCnt number of buffers in bufVec to receive
    * @param[in] flags
    * @return awaiter{int ret}
    *   @retval ret>=0, ok. ret is the size of transfered data.
    *   @retval ret<0, -ret is the error code.
    * @note bufCnt should be >0 and <= MAX_BUFS_IN_IOVEC(8), else get an EINVAL err
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockRecvVecFrom(SOCKET sock, SockAddr& addr, iovec* bufVec, size_t bufCnt, int flags = 0) noexcept {
        return LazyRecvVecFrom(sock, addr, bufVec, bufCnt, flags);
    }

    /*
    * @brief Receive data exactly using socket
    * @param[in] sock a socket
    * @param[in] buf
    * @param[in] n number of data bytes to receive
    * @param[in] flags
    * @return awaiter{int err}
    *   @retval err=0, ok. Received n bytes data exactly.
    *   @retval err<>0, the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockMustRecv(SOCKET sock, void* buf, uint32_t n, int flags = 0) noexcept {
        return LazyMustRecv(sock, buf, n, flags);
    }

    /*
    * @brief Receive data exactly using socket
    * @param[in] sock a socket
    * @param[in] bufVec
    * @param[in] bufCnt number of buffers in bufVec to receive
    * @param[in] flags
    * @return awaiter{int err}
    *   @retval err=0, ok. Received n bytes data exactly.
    *   @retval err<>0, the error code.
    * @note bufCnt should be >0 and <= MAX_BUFS_IN_IOVEC(8), else get an EINVAL err
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockMustRecvVec(SOCKET sock, iovec* bufVec, size_t bufCnt, int flags = 0) noexcept {
        return LazyMustRecvVec(sock, bufVec, bufCnt, flags);
    }

    /*
    * @brief Send data exactly using socket
    * @param[in] sock a socket
    * @param[in] buf
    * @param[in] n number of data bytes to send
    * @param[in] flags
    * @return awaiter{int err}
    *   @retval err=0, ok. Sent n bytes data exactly.
    *   @retval err<>0, the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockMustSend(SOCKET sock, const void* buf, uint32_t n, int flags = 0) noexcept {
        return LazyMustSend(sock, buf, n, flags);
    }

    /*
    * @brief Send data exactly using socket
    * @param[in] sock a socket
    * @param[in] bufVec
    * @param[in] bufCnt number of buffers in bufVec to send
    * @param[in] flags
    * @return awaiter{int err}
    *   @retval err=0, ok. Sent n bytes data exactly.
    *   @retval err<>0, the error code.
    * @note bufCnt should be >0 and <= MAX_BUFS_IN_IOVEC(8), else get an EINVAL err
    */
    [[BLACO_AWAIT_HINT]]
    inline auto SockMustSendVec(SOCKET sock, const iovec* bufVec, size_t bufCnt, int flags = 0) noexcept {
        return LazyMustSendVec(sock, bufVec, bufCnt, flags);
    }

    /*
    * @brief Shutdown the writing side of a socket
    * @param[in] sock a socket
    * @return awaiter{int err}
    *   @retval err=0, ok.
    *   @retval err<>0, the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto TcpShutdown(SOCKET sock, int how) noexcept {
        return LazyTcpShutdown(sock, how);
    }

    /*
    * @brief Close the socket
    * @param[in] sock a socket
    * @return awaiter{int err}
    *   @retval err=0, ok.
    *   @retval err<>0, the error code.
    */
    [[BLACO_AWAIT_HINT]]
    inline auto TcpClose(SOCKET sock) noexcept {
        return LazyTcpClose(sock);
    }

    typedef std::function<void(const char*, int)> FnTcpServerLog;
    typedef std::function<task<void>(SOCKET, const struct sockaddr&, FnTcpServerLog fnLog)> FnStartTcpSession;

    inline task<void> _TcpAcceptLoop(SOCKET sock, sa_family_t family, FnStartTcpSession fn, FnTcpServerLog fnLog) {
        SockAddr peer;
        for (;;) {
            int r = co_await TcpAccept(sock, family, &peer);
            if (r < 0) {
                fnLog("TcpAccept failed", -r);
                break;
            }
            bl::go(fn(r, *peer.get_sockaddr(), fnLog).get_handle(), true);
        }
    }

    /*
    * @brief Start a TCP server
    * @param[in] sock the listening socket of server
    * @param[in] fn a function object of type task<void>(SOCKET sessionSock, const SockAddr& peer)
    * @param[in] fnLog a function object of type void(const char* s, int r), for logging
    * @param[in] numConcurrentAccepts num of concurrent accept calls to be post into ioworkers
    * @warning In fn, peer is a temp object, you should copy it ASAP before next suspend point of the current coroutine.
    */
    inline void TcpStartServer(SOCKET sock, sa_family_t family, FnStartTcpSession fn, FnTcpServerLog fnLog, size_t numConcurrentAccepts = 0) {
        if (numConcurrentAccepts == 0)
            numConcurrentAccepts = g_numCpus;
        for (size_t i = 0; i < numConcurrentAccepts; ++i) {
            auto t = _TcpAcceptLoop(sock, family, fn, fnLog);
            bl::go(t.get_handle(), true); // schedule the coroutine to make it balanced
        }
    }

    struct LazyFileRead : LazyIoBase<BlFileRead_t, int> {
        LazyFileRead(int f, uint64_t offset, void* buf, uint32_t len) {
            BlInitFileRead(this, f, offset, buf, len, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoFileRead(this);
        }
    };

    inline auto FileRead(int f, uint64_t offset, void* buf, uint32_t len) {
        return LazyFileRead(f, offset, buf, len);
    }

    struct LazyFileReadVec : LazyIoBase<BlFileReadVec_t, int> {
        LazyFileReadVec(int f, uint64_t offset, struct iovec* bufs, size_t bufCnt) {
            BlInitFileReadVec(this, f, offset, bufs, bufCnt, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoFileReadVec(this);
        }
    };

    inline auto FileReadVec(int f, uint64_t offset, struct iovec* bufs, size_t bufCnt) {
        return LazyFileReadVec(f, offset, bufs, bufCnt);
    }

    struct LazyFileWrite : LazyIoBase<BlFileWrite_t, int> {
        LazyFileWrite(int f, uint64_t offset, const void* buf, uint32_t len) {
            BlInitFileWrite(this, f, offset, buf, len, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoFileWrite(this);
        }
    };

    inline auto FileWrite(int f, uint64_t offset, const void* buf, uint32_t len) {
        return LazyFileWrite(f, offset, buf, len);
    }

    struct LazyFileWriteVec : LazyIoBase<BlFileWriteVec_t, int> {
        LazyFileWriteVec(int f, uint64_t offset, const struct iovec* bufs, size_t bufCnt) {
            BlInitFileWriteVec(this, f, offset, bufs, bufCnt, onCompleted);
        }

        bool await_suspend(std::coroutine_handle<> current) {
            set_resume_point(current);
            return BlDoFileWriteVec(this);
        }
    };

    inline auto FileWriteVec(int f, uint64_t offset, const struct iovec* bufs, size_t bufCnt) {
        return LazyFileWriteVec(f, offset, bufs, bufCnt);
    }

    struct LazySleep {
        static void onTimer(void* coro) {
            std::coroutine_handle<>::from_address(coro).resume();
        }

        bool await_ready() const noexcept { return false; }
        void await_resume() const noexcept {}
        auto await_suspend(std::coroutine_handle<> h) noexcept {
            if (ms <= 0)
                return false;
            BlAddOnceTimer(-((int64_t)ms), onTimer, h.address());
            return true;
        }

        uint32_t ms;
    };

    inline auto CoSleep(uint32_t ms) {
        return LazySleep{ ms };
    }

    /*
    * @brief C++ helper of BlCutex
    * @note usage:
    * @code
    *   BlCutex cutex;
    *   BlCutexInit(&cutex);
    *   // ...
    *   {
    *     bl::ScopedCutex scopedCutex(&cutex);
    *     co_await scopedCutex;
    *     // ... locked, do something ...
    *   } // auto unlocked after scopedCutex run out of scope
    * @endcode
    */
    class ScopedCutex {
        BlCutex *cutex;
        BlCutexCoro waiter;

    public:
        explicit ScopedCutex(BlCutex* aCutex) : cutex(aCutex) {}

        ScopedCutex(const ScopedCutex&) = delete;
        auto operator=(const ScopedCutex&) = delete;

        ScopedCutex(ScopedCutex&& other): cutex(std::exchange(other.cutex, nullptr)) {}

        ScopedCutex& operator=(ScopedCutex&& other) noexcept {
            if (std::addressof(other) != this)
                cutex = std::exchange(other.cutex, nullptr);
            return *this;
        }

        ~ScopedCutex() { if(cutex) BlCutexUnlock(cutex); }

        static void onLocked(void* coro) {
            std::coroutine_handle<>::from_address(coro).resume();
        }

        bool await_ready() const noexcept { return false; }
        void await_resume() const noexcept {}
        auto await_suspend(std::coroutine_handle<> h) noexcept {
            if (BlCutexTryLock(cutex))
                return false;
            waiter.coro = h.address();
            waiter.onLocked = onLocked;
            return !BlCutexLock(cutex, &waiter);
        }
    };
} // end of namespace bl
