//  Copyright (c) 2007-2016 Hartmut Kaiser
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_fwd.hpp>
#include <hpx/runtime/threads/thread_data.hpp>
#include <hpx/runtime/threads/policies/scheduler_mode.hpp>
#include <hpx/runtime/threads/detail/create_thread.hpp>
#include <hpx/runtime/threads/detail/set_thread_state.hpp>
#include <hpx/runtime/threads/executors/current_executor.hpp>
#include <hpx/util/bind.hpp>
#include <hpx/util/register_locks.hpp>

namespace hpx { namespace threads { namespace executors { namespace detail
{
    ///////////////////////////////////////////////////////////////////////////
    current_executor::current_executor(
            policies::scheduler_base* scheduler)
      : scheduler_base_(scheduler)
    {}

    threads::thread_state_enum
    current_executor::thread_function_nullary(closure_type func)
    {
        // execute the actual thread function
        func();

        // Verify that there are no more registered locks for this
        // OS-thread. This will throw if there are still any locks
        // held.
        util::force_error_on_lock();

        return threads::terminated;
    }

    // Schedule the specified function for execution in this executor.
    // Depending on the subclass implementation, this may block in some
    // situations.
    void current_executor::add(
        closure_type && f, util::thread_description const& desc,
        threads::thread_state_enum initial_state,
        bool run_now, threads::thread_stacksize stacksize, error_code& ec)
    {
        // create a new thread
        thread_init_data data(util::bind(
            util::one_shot(&current_executor::thread_function_nullary),
            std::move(f)), desc);
        data.stacksize = threads::get_stack_size(stacksize);

        threads::thread_id_type id = threads::invalid_thread_id;
        threads::detail::create_thread(scheduler_base_, data, id,
            initial_state, run_now, ec);
        if (ec) return;

        if (&ec != &throws)
            ec = make_success_code();
    }

    void current_executor::add_at(
        boost::chrono::steady_clock::time_point const& abs_time,
        closure_type && f, util::thread_description const& desc,
        threads::thread_stacksize stacksize, error_code& ec)
    {
        // create a new suspended thread
        thread_init_data data(util::bind(
            util::one_shot(&current_executor::thread_function_nullary),
            std::move(f)), desc);
        data.stacksize = threads::get_stack_size(stacksize);

        threads::thread_id_type id = threads::invalid_thread_id;
        threads::detail::create_thread( //-V601
            scheduler_base_, data, id, suspended, true, ec);
        if (ec) return;
        HPX_ASSERT(invalid_thread_id != id);    // would throw otherwise

        // now schedule new thread for execution
        threads::detail::set_thread_state_timed(
            *scheduler_base_, abs_time, id, ec);
        if (ec) return;

        if (&ec != &throws)
            ec = make_success_code();
    }

    // Schedule given function for execution in this executor no sooner
    // than time rel_time from now. This call never blocks, and may
    // violate bounds on the executor's queue size.
    void current_executor::add_after(
        boost::chrono::steady_clock::duration const& rel_time,
        closure_type && f, util::thread_description const& desc,
        threads::thread_stacksize stacksize, error_code& ec)
    {
        return add_at(boost::chrono::steady_clock::now() + rel_time,
            std::move(f), desc, stacksize, ec);
    }

    // Return an estimate of the number of waiting tasks.
    boost::uint64_t current_executor::num_pending_closures(
        error_code& ec) const
    {
        return scheduler_base_->get_thread_count() -
                    scheduler_base_->get_thread_count(terminated);
    }

    // Reset internal (round robin) thread distribution scheme
    void current_executor::reset_thread_distribution()
    {
        scheduler_base_->reset_thread_distribution();
    }

    // Return the requested policy element
    std::size_t current_executor::get_policy_element(
        threads::detail::executor_parameter p, error_code& ec) const
    {
        switch(p) {
        case threads::detail::min_concurrency:
        case threads::detail::max_concurrency:
        case threads::detail::current_concurrency:
            return hpx::get_os_thread_count();

        default:
            break;
        }

        HPX_THROWS_IF(ec, bad_parameter,
            "thread_pool_executor::get_policy_element",
            "requested value of invalid policy element");
        return std::size_t(-1);
    }

    hpx::state current_executor::get_state() const
    {
        return scheduler_base_->get_state(hpx::get_worker_thread_num());
    }

    void current_executor::set_scheduler_mode(
        threads::policies::scheduler_mode mode)
    {
        return scheduler_base_->set_scheduler_mode(mode);
    }
}}}}

namespace hpx { namespace threads { namespace executors
{
    ///////////////////////////////////////////////////////////////////////////
    // this is just a wrapper around a scheduler_base assuming the wrapped
    // scheduler outlives the wrapper
    current_executor::current_executor()
      : scheduled_executor(new detail::current_executor(
            get_self_id()->get_scheduler_base()))
    {}

    current_executor::current_executor(policies::scheduler_base* scheduler)
      : scheduled_executor(new detail::current_executor(scheduler))
    {}

    hpx::state current_executor::get_state() const
    {
        return boost::static_pointer_cast<
                detail::current_executor
            >(executor::executor_data_)->get_state();
    }
}}}
