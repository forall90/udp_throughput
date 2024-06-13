#include <iostream>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/array.hpp>

using namespace std;

namespace ba = boost::asio;
using ba::ip::udp;
using chrono::steady_clock;

class Data
{
    chrono::duration<int> period_;
    chrono::steady_clock::time_point check_time_;
    int send_packet_;
    int32_t counter_;
    boost::array<char, 1024> buffer_;

public:
    Data() : period_(chrono::seconds(1)), send_packet_(0), counter_(0)
    {
        check_time_ = steady_clock::now() + period_;
    }

    ba::const_buffer get()
    {
        ++send_packet_;
        ++counter_;

        memcpy(&buffer_[4], &counter_, sizeof(counter_));
        if (counter_ % 1000 == 0)
        {
            const auto now = steady_clock::now();
            if (now > check_time_)
            {
                cout << counter_ << ":" << send_packet_ << endl;
                check_time_       += period_;
                send_packet_  = 0;
            }
        }
        return ba::buffer(buffer_);
    }
};

int main(int argc, char* argv[])
{
    ba::io_service  io_service;
    string dst = "127.0.0.1";
    if (argc > 1)
        dst = argv[1];

    const udp::endpoint endpoint(ba::ip::address::from_string(dst), 5432);
    udp::socket socket(io_service, ba::ip::udp::v4());

    Data data;
   
    while (true)
    {
        socket.send_to(data.get(), endpoint);
    }
}
