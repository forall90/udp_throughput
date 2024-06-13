#include <chrono>
#include <iomanip>
#include <iostream>

#include <thread>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/timer/timer.hpp>

#if !defined(_WIN32)
#define USE_RECVMMSG
#endif

using namespace std;

namespace ba = boost::asio;
namespace bs = boost::system;
using ba::ip::udp;
using chrono::steady_clock;

struct ResStat
{
    int pps;
    int errors;
    int cpu_usage;
    int sys_usage;
};

class Counter
{
    int                     count_;
    int                     errors_;
    boost::timer::cpu_timer cpu_timer_;
    const double            SEC = 1000000000.0L;

public:
    Counter()
        : count_(0)
    {
        reset();
    }

    void reset()
    {
        count_  = 0;
        errors_ = 0;
        cpu_timer_.start();
    }

    double elapsed() const
    {
        return cpu_timer_.elapsed().wall / SEC;
    }

    int inc(bool valid)
    {
        ++(valid ? count_ : errors_);
        return count_ + errors_;
    }

    ResStat stat() const
    {
        const boost::timer::cpu_times cpu_times = cpu_timer_.elapsed();

        const double sys_sec   = static_cast<double>(cpu_times.system) / SEC;
        const double user_sec  = static_cast<double>(cpu_times.user) / SEC;
        double       total_sec = sys_sec + user_sec;
        double       wall_sec  = static_cast<double>(cpu_times.wall) / SEC;
        wall_sec               = max(wall_sec, 0.01);
        total_sec              = max(total_sec, 0.01);

        double sys_p = sys_sec / total_sec;
        sys_p        = min(1.0, sys_p);
        double p     = total_sec / wall_sec;
        p            = std::max(0.01, p);

        return {
            static_cast<int>(count_ / wall_sec),
            errors_,
            static_cast<int>(p * 100.0),
            static_cast<int>(sys_p * 100.0)};
    }
};

class Checker
{
    int32_t seqn_;

public:
    Checker()
        : seqn_(0)
    {
    }

    bool check(const char *buffer, std::size_t bytes_transferred)
    {
        const int32_t seqn  = get_seqn(buffer, bytes_transferred);
        const bool    valid = !seqn || !seqn_ || seqn == seqn_ + 1;
        seqn_               = seqn;
        return valid;
    }

protected:
    static int32_t get_seqn(const char *buffer, std::size_t bytes_transferred)
    {
        const size_t offset = 4;
        int          res    = 0;
        if (buffer && bytes_transferred >= sizeof(int) + offset)
            memcpy(&res, buffer + offset, sizeof(int));
        return res;
    }
};

class Processor
{
    Counter cnt_period_;
    Counter cnt_all_;
    Checker checker_;

public:
    const bool verbose_;

public:
    Processor()
        : verbose_(false)
    {
    }

    void on_data(const char *buffer, std::size_t bytes_transferred)
    {
        const bool valid = checker_.check(buffer, bytes_transferred);
        cnt_all_.inc(valid);

        if (cnt_period_.inc(valid) % 1000 == 0)
        {
            if (cnt_period_.elapsed() > 1.0)
            {
                show_stat(cnt_period_.stat());
                cnt_period_.reset();
            }
        }
    }

    ResStat stat() const
    {
        return cnt_all_.stat();
    }

protected:
    void show_stat(const ResStat &stat) const
    {
        if (verbose_)
        {
            cout << stat.pps << " pps, errors: " << stat.errors
                 << " cpu=" << stat.cpu_usage << "% (s/u: " << stat.sys_usage << "/" << 100 - stat.sys_usage << ") "
                 << " ppc = " << stat.pps / stat.cpu_usage * 100 << endl;
        }
        else
        {
            cout << (stat.errors ? 'E' : '.');
            cout.flush();
        }
    }
};

class BaseReader
{
protected:
    Processor                  processor_;
    ba::io_service             io_service_;
    udp::socket                socket_;
    boost::array<char, 1024>   buf_;
    const chrono::milliseconds delay_;

public:
    BaseReader(int delay)
        : socket_(io_service_, ba::ip::udp::v4())
        , delay_(chrono::milliseconds(delay))
    {
        const udp::endpoint endpoint(ba::ip::address::from_string("0.0.0.0"), 5432);
        socket_.bind(endpoint);
        set_read_buf_size(1 << 22);
    }

    void stop()
    {
        socket_.close();
        io_service_.stop();
    }

    ResStat stat() const
    {
        return processor_.stat();
    }

    virtual string name() const = 0;
    virtual void   run()        = 0;

protected:
    void update_stat(std::size_t bytes_transferred)
    {
        processor_.on_data(buf_.data(), bytes_transferred);
    }

    void set_read_buf_size(size_t read_buf_size)
    {
        socket_.set_option(udp::socket::receive_buffer_size(read_buf_size));
        udp::socket::receive_buffer_size actual;
        socket_.get_option(actual);
        if ((std::size_t)actual.value() != read_buf_size)
        {
            cerr << "Warning: requested socket buffer size of " << read_buf_size
                 << " but actual size is " << actual.value() << endl;
        }
    }
};

class AsioSyncReader : public BaseReader
{
public:
    AsioSyncReader(int delay)
        : BaseReader(delay)
    {
    }

    void run() override
    {
        if (processor_.verbose_)
            cout << "=== " << name() << endl;

        udp::endpoint remote;
        while (true)
        {
            bs::error_code ec;
            std::size_t    bytes_transferred = socket_.receive_from(ba::buffer(buf_), remote, 0, ec);
            if (ec)
                break;
            update_stat(bytes_transferred);
        }
    }

    string name() const override
    {
        return "ReadSync";
    }
};

class AsioPeriodicReader : public BaseReader
{
public:
    AsioPeriodicReader(int delay)
        : BaseReader(delay)
    {
    }

    void run() override
    {
        if (processor_.verbose_)
            cout << "=== " << name() << endl;

        socket_.non_blocking(true);
        udp::endpoint remote;

        while (true)
        {
            while (true)
            {
                bs::error_code ec;
                std::size_t    bytes_transferred = socket_.receive_from(ba::buffer(buf_), remote, 0, ec);
                if (ec == ba::error::would_block)
                    break;
                else if (ec)
                    return;
                update_stat(bytes_transferred);
            }
            if (delay_.count())
                this_thread::sleep_for(delay_);
        }
    }

    string name() const override
    {
        if (delay_.count())
            return "Non Block with Sleep";
        else
            return "Non Block";
    }
};

class AsioAsyncReader : public BaseReader
{
    const size_t                           poll_ = 1000;
    ba::basic_waitable_timer<steady_clock> timer_;
    udp::endpoint                          remote_;

    void enqueue_receive()
    {
        using namespace std::placeholders;
        socket_.async_receive_from(
            ba::buffer(buf_),
            remote_,
            std::bind(&AsioAsyncReader::packet_handler, this, _1, _2));
    }

    void packet_handler(const bs::error_code &error,
                        std::size_t           bytes_transferred)
    {
        if (error)
            return;

        update_stat(bytes_transferred);

        for (size_t i = 0; i < poll_; i++)
        {
            bs::error_code ec;
            bytes_transferred = socket_.receive_from(ba::buffer(buf_), remote_, 0, ec);
            if (ec == ba::error::would_block)
            {
                if (delay_.count() > 0)
                {
                    enqueue_wait();
                    return;
                }
                break;
            }
            else if (ec)
                return;
            update_stat(bytes_transferred);
        }
        enqueue_receive();
    }

    void enqueue_wait()
    {
        timer_.async_wait(std::bind(&AsioAsyncReader::enqueue_receive, this));
        timer_.expires_from_now(delay_);
    }

public:
    AsioAsyncReader(int delay)
        : BaseReader(delay)
        , timer_(io_service_)
    {
    }

    void run() override
    {
        if (processor_.verbose_)
            cout << "=== " << name() << endl;
        socket_.non_blocking(true);
        enqueue_receive();
        io_service_.run();
    }

    string name() const override
    {
        if (delay_.count())
            return "ASync with Sleep";
        else
            return "ASync";
    }
};

#ifdef USE_RECVMMSG

#include <sys/timerfd.h>

class RecvMsg : public BaseReader
{
    static constexpr int        batch_size_ = 64;
    const size_t                poll_       = 1000;
    std::vector<std::uint8_t>   buffer_;
    std::vector<struct mmsghdr> msgvec_;
    std::vector<struct iovec>   iovec_;

public:
    RecvMsg(int delay)
        : BaseReader(delay)
    {
        init_buffer();
    }

    void run() override
    {
        if (processor_.verbose_)
            cout << "=== " << name() << endl;

        pollfd fds[1] = {};
        fds[0].fd     = socket_.native_handle();
        fds[0].events = POLLIN;

        while (true)
        {
            int result = poll(fds, std::size(fds), -1);
            if (result < 0)
                return;

            if (fds[0].revents & POLLIN)
            {
                for (int i = 0; i <= poll_; i++)
                {
                    int result = recvmmsg(socket_.native_handle(), msgvec_.data(), msgvec_.size(), 0, NULL);
                    if (result < 0)
                    {
                        if (errno == EAGAIN)
                        {
                            //std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            break;
                        }
                        if (errno == EBADF)
                        {
                            return;
                        }
                        throw std::system_error(errno, std::system_category());
                    }
                    for (int i = 0; i < result; i++)
                    {
                        processor_.on_data(static_cast<const char *>(iovec_[i].iov_base), msgvec_[i].msg_len);
                    }
                }
            }
        }
    }

    string name() const override
    {
        return "RecvMsg";
    }

private:
    void init_buffer()
    {
        buffer_.resize(batch_size_ * buf_.size());
        msgvec_.resize(batch_size_);
        iovec_.resize(batch_size_);

        for (int i = 0; i < batch_size_; i++)
        {
            iovec   &iov = iovec_[i];
            mmsghdr &mgh = msgvec_[i];

            iov.iov_base = buffer_.data() + buf_.size() * i;
            iov.iov_len  = buf_.size();

            std::memset(&mgh, 0, sizeof(mgh));
            mgh.msg_hdr.msg_iov    = &iov;
            mgh.msg_hdr.msg_iovlen = 1;
        }
    }
};

#endif // USE_RECVMMSG

template <typename T>
string start_receiver(int delay, int work_time)
{
    T receiver(delay);

    std::thread t([&receiver]
                  { receiver.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(work_time));
    receiver.stop();
    t.join();

    ResStat      s = receiver.stat();
    stringstream ss;
    ss << setw(30) << receiver.name() << "| pps=" << setw(10) << s.pps << " | cpu=" << setw(3) << s.cpu_usage << "% | errors=" << s.errors;
    return ss.str();
}

int main(int argc, char *argv[])
{
    int work_time = 3;
    if (argc > 1)
        work_time = atoi(argv[1]);
    work_time = max(work_time, 1);

    struct Cases
    {
        int delay;
        string (*func)(int, int);
    } cases[] = {
        { 0,     start_receiver<AsioSyncReader>},
        { 0, start_receiver<AsioPeriodicReader>},
        {10, start_receiver<AsioPeriodicReader>},
        { 0,    start_receiver<AsioAsyncReader>},
        {10,    start_receiver<AsioAsyncReader>},
#ifdef USE_RECVMMSG
        { 0,            start_receiver<RecvMsg>},
#endif
    };

    stringstream ss;
    for (auto &c : cases)
        ss << c.func(c.delay, work_time) << endl;

    cout << endl << ss.str();
    return 0;
}
