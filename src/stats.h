#ifndef PERF_STATS_H
#define PERF_STATS_H

#include <vector>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <iomanip>

class Stats {
public:
    void add(double latency_us) {
        latencies_us.push_back(latency_us);
    }

    void calculate() {
        if (latencies_us.empty()) {
            std::cout << "No data to calculate stats." << std::endl;
            return;
        }

        std::sort(latencies_us.begin(), latencies_us.end());

        double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
        double avg = sum / latencies_us.size();

        double median = 0;
        size_t mid = latencies_us.size() / 2;
        if (latencies_us.size() % 2 == 0) {
            median = (latencies_us[mid - 1] + latencies_us[mid]) / 2.0;
        } else {
            median = latencies_us[mid];
        }

        size_t p99_index = static_cast<size_t>(latencies_us.size() * 0.99);
        if (p99_index >= latencies_us.size()) {
            p99_index = latencies_us.size() - 1;
        }
        double p99 = latencies_us[p99_index];

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Samples: " << latencies_us.size() << std::endl;
        std::cout << "Average: " << avg << " us" << std::endl;
        std::cout << "Median (50th): " << median << " us" << std::endl;
        std::cout << "99th Percentile: " << p99 << " us" << std::endl;
    }

private:
    std::vector<double> latencies_us;
};

#endif // PERF_STATS_H