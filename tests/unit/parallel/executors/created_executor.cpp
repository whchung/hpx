//  Copyright (c) 2015 Daniel Bourgeois
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_init.hpp>
#include <hpx/hpx.hpp>
#include <hpx/include/parallel_executors.hpp>
#include <hpx/parallel/executors/executor_traits.hpp>
#include <hpx/util/lightweight_test.hpp>
#include <hpx/util/deferred_call.hpp>

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <string>
#include <vector>

#include <boost/range/iterator_range.hpp>
#include <boost/range/functions.hpp>

using hpx::parallel::parallel_executor;
using hpx::util::deferred_call;
typedef std::vector<int>::iterator iter;

////////////////////////////////////////////////////////////////////////////////
// A parallel executor that returns void for bulk_execute and hpx::future<void>
// for bulk_async_execute
struct void_parallel_executor : parallel_executor
{
    template <typename F, typename Shape>
    std::vector<hpx::future<void> >
    bulk_async_execute(F && f, Shape const& shape)
    {
        std::vector<hpx::future<void> > results;
        for(auto const& elem: shape)
        {
            results.push_back(
                this->parallel_executor::async_execute(deferred_call(f, elem))
            );
        }
        return results;
    }

    template <typename F, typename Shape>
    void bulk_execute(F && f, Shape const& shape)
    {
        return hpx::util::unwrapped(
            bulk_async_execute(std::forward<F>(f), shape));
    }
};

////////////////////////////////////////////////////////////////////////////////
// Tests to void_parallel_executor behavior for the bulk executes

void bulk_test(hpx::thread::id tid, int value)
{
    HPX_TEST(tid != hpx::this_thread::get_id());
}

void test_void_bulk_sync()
{
    typedef void_parallel_executor executor;
    typedef hpx::parallel::executor_traits<executor> traits;

    hpx::thread::id tid = hpx::this_thread::get_id();

    std::vector<int> v(107);
    std::iota(boost::begin(v), boost::end(v), std::rand());

    using hpx::util::placeholders::_1;

    executor exec;
    traits::execute(exec, hpx::util::bind(&bulk_test, tid, _1), v);
}

void test_void_bulk_async()
{
    typedef void_parallel_executor executor;
    typedef hpx::parallel::executor_traits<executor> traits;

    hpx::thread::id tid = hpx::this_thread::get_id();

    std::vector<int> v(107);
    std::iota(boost::begin(v), boost::end(v), std::rand());

    using hpx::util::placeholders::_1;

    executor exec;
    hpx::when_all(
        traits::async_execute(exec, hpx::util::bind(&bulk_test, tid, _1), v)
    ).get();
}

////////////////////////////////////////////////////////////////////////////////
// Sum using hpx's parallel_executor and the above void_parallel_executor

// Create shape argument for parallel_executor
std::vector<boost::iterator_range<iter> >
split(iter first, iter last, int parts)
{
    typedef std::iterator_traits<iter>::difference_type sz_type;
    sz_type count = std::distance(first, last);
    sz_type increment = count/parts;

    std::vector<boost::iterator_range<iter> > results;
    while(first != last)
    {
        iter prev = first;
        std::advance(
            first,
            (std::min)(increment, std::distance(first,last))
        );
        results.push_back(boost::make_iterator_range(prev, first));
    }
    return results;
}

// parallel sum using hpx's parallel executor
int parallel_sum(iter first, iter last, int num_parts)
{
    parallel_executor exec;
    typedef hpx::parallel::executor_traits<parallel_executor> traits;

    std::vector<boost::iterator_range<iter> > input =
        split(first, last, num_parts);

    std::vector<hpx::future<int> > v =
        traits::async_execute(exec,
            [](boost::iterator_range<iter> const& rng) -> int
            {
                return std::accumulate(boost::begin(rng), boost::end(rng), 0);
            },
            input);

    return std::accumulate(
        boost::begin(v), boost::end(v), 0,
        [](int a, hpx::future<int>& b) -> int
        {
            return a + b.get();
        });
}

// parallel sum using void parallel executer
int void_parallel_sum(iter first, iter last, int num_parts)
{
    void_parallel_executor exec;
    typedef hpx::parallel::executor_traits<void_parallel_executor> traits;

    std::vector<int> temp(num_parts + 1, 0);
    std::iota(boost::begin(temp), boost::end(temp), 0);

    std::ptrdiff_t section_size = std::distance(first, last) / num_parts;

    std::vector<hpx::future<void> > f = traits::async_execute(exec,
        [&](const int& i)
        {
            iter b = first + i*section_size;
            iter e = first + (std::min)(
                    std::distance(first, last),
                    static_cast<std::ptrdiff_t>((i+1)*section_size)
                );
            temp[i] = std::accumulate(b, e, 0);
        },
        temp);

    hpx::when_all(f).get();

    return std::accumulate(boost::begin(temp), boost::end(temp), 0);
}

void sum_test()
{
    std::vector<int> vec(10007);
    auto random_num = [](){ return std::rand() % 50 - 25; };
    std::generate(boost::begin(vec), boost::end(vec), random_num);

    int sum = std::accumulate(boost::begin(vec), boost::end(vec), 0);
    int num_parts = std::rand() % 5 + 3;

    // Return futures holding results of parallel_sum and void_parallel_sum
    parallel_executor exec;

    typedef hpx::parallel::executor_traits<parallel_executor> traits;
    hpx::future<int> f_par =
        traits::async_execute(exec, deferred_call(&parallel_sum,
            boost::begin(vec), boost::end(vec), num_parts));

    hpx::future<int> f_void_par =
        traits::async_execute(exec, deferred_call(&void_parallel_sum,
            boost::begin(vec), boost::end(vec), num_parts));

    HPX_TEST(f_par.get() == sum);
    HPX_TEST(f_void_par.get() == sum);
}

////////////////////////////////////////////////////////////////////////////////
int hpx_main(boost::program_options::variables_map& vm)
{
    unsigned int seed = static_cast<unsigned int>(std::time(0));
    if (vm.count("seed"))
        seed = vm["seed"].as<unsigned int>();

    std::cout << "using seed: " << seed << std::endl;
    std::srand(seed);

    test_void_bulk_sync();
    test_void_bulk_async();
    sum_test();
    return hpx::finalize();
}

int main(int argc, char* argv[])
{
    // add command line option which controls the random number generator seed
    using namespace boost::program_options;
    options_description desc_commandline(
        "Usage: " HPX_APPLICATION_STRING " [options]");

    desc_commandline.add_options()
        ("seed,s", value<unsigned int>(),
        "the random number generator seed to use for this run")
        ;

    // By default this test should run on all available cores
    std::vector<std::string> cfg;
    cfg.push_back("hpx.os_threads=" +
        std::to_string(hpx::threads::hardware_concurrency()));

    // Initialize and run HPX
    HPX_TEST_EQ_MSG(hpx::init(desc_commandline, argc, argv, cfg), 0,
        "HPX main exited with non-zero status");

    return hpx::util::report_errors();
}
