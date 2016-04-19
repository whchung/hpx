//  Copyright (c) 2015 Marcin Copik
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

/// \file parallel/executors/sequential_executor.hpp

#if !defined(HPX_PARALLEL_EXECUTORS_GPU_SYCL_EXECUTOR)
#define HPX_PARALLEL_EXECUTORS_GPU_SYCL_EXECUTOR

#include <hpx/config.hpp>
#include <hpx/parallel/config/inline_namespace.hpp>
#include <hpx/parallel/exception_list.hpp>
#include <hpx/parallel/executors/executor_traits.hpp>
#include <hpx/runtime/threads/thread_executor.hpp>
#include <hpx/util/decay.hpp>
#include <hpx/util/result_of.hpp>
#include <hpx/util/unwrapped.hpp>

#include <type_traits>
#include <utility>
#include <iterator>

#include <boost/range/functions.hpp>
#include <boost/range/const_iterator.hpp>
#include <boost/type_traits/is_void.hpp>

#include <SYCL/sycl.hpp>

namespace hpx { namespace parallel { HPX_INLINE_NAMESPACE(v3)
{

	///////////////////////////////////////////////////////////////////////////
    /// A \a sequential_executor creates groups of sequential execution agents
    /// which execute in the calling thread. The sequential order is given by
    /// the lexicographical order of indices in the index space.
    ///
    struct gpu_sycl_executor
    {
		#if defined(DOXYGEN)
				/// Create a new sequential executor
				gpu_sycl_executor() {}
		#endif
		
		template<typename ValueType, typename BufferType>
		struct gpu_sycl_buffer_view_wrapper
		{
			ValueType & operator[](std::size_t idx);
		};

		template<typename Iter,
				typename value_type = typename std::iterator_traits<Iter>::value_type,
    			typename buffer_type = cl::sycl::buffer<typename std::iterator_traits<Iter>::value_type, 1>,
				typename buffer_view_type =  decltype( std::declval< buffer_type >().template get_access<cl::sycl::access::mode::read_write>( std::declval<cl::sycl::handler &>() ) )>
    	struct gpu_sycl_buffer : detail::gpu_executor_buffer<Iter, buffer_view_type>
		{
			cl::sycl::default_selector selector;
			cl::sycl::queue queue;
    		Iter cpu_buffer;
    		std::shared_ptr<buffer_type> buffer;
			// TODO: possibly not safe, change letter
    		buffer_view_type * _buffer_view;

    		gpu_sycl_buffer(Iter first, std::size_t count) :
				queue( selector ),
    			cpu_buffer( first ),
				_buffer_view( nullptr )
    		{
    			Iter last = first;
    			std::advance(last, count);

    			buffer.reset( new buffer_type(first, last) );
    		}

    		buffer_view_type * buffer_view()
    		{
    			return _buffer_view;
    		}

    		void sync()
    		{
    		}

    		void print()
    		{
    			std::cout << "buffer: " << *cpu_buffer << std::endl;
    		}
		};

    	template<typename Iter>
    	static gpu_sycl_buffer<Iter> create_buffers(Iter first, std::size_t count)
		{
    		return gpu_sycl_buffer<Iter>(first, count);
		}

    	template<typename Iter>
    	static std::shared_ptr<gpu_sycl_buffer<Iter>> create_buffers_shared(Iter first, std::size_t count)
		{
    		return std::make_shared<gpu_sycl_buffer<Iter>>(first, count);
		}

		template <typename F>
		static typename hpx::util::result_of<
			typename hpx::util::decay<F>::type()
		>::type
		execute(F && f)
		{
			throw std::runtime_error("Feature not supported in GPU AMP executor! Please, use bulk execute.");
		}

		template <typename F>
		static hpx::future<typename hpx::util::result_of<
			typename hpx::util::decay<F>::type()
		>::type>
		async_execute(F && f)
		{
			throw std::runtime_error("Feature not supported in GPU AMP executor! Please, use bulk execute.");
		}

		template <typename F, typename Shape, typename GPUBuffer>
		static std::vector<hpx::future<
			typename detail::bulk_async_execute_result<F, Shape>::type
		> >
		bulk_async_execute(F && f, Shape const& shape, GPUBuffer &)
		{
			typedef typename detail::bulk_async_execute_result<F, Shape>::type result_type;
			std::vector<hpx::future<result_type> > results;

			try {
				for (auto const& elem: shape) {
					std::size_t x = elem.first;
					std::size_t y = elem.second;			
					//results.push_back(hpx::async(launch::async,
						/**
						 * Lambda calling the AMP parallel execution.
						 */
						/*[=](std::size_t x, std::size_t y) {
							Concurrency::extent<1> e(y);
							Concurrency::parallel_for_each(e, [=](Concurrency::index<1> idx) restrict(amp)
							{
								auto _x = std::make_pair(x + idx[0], 1);
								f(_x);
							});
						},*/
						/**
						 * Args of lambda - start position and size
						 */
						//x, y));
						
				}
			}
			catch (std::bad_alloc const& ba) {
				boost::throw_exception(ba);
			}
			catch (...) {
				boost::throw_exception(
				    exception_list(boost::current_exception())
				);
			}

			return std::move(results);
		}

		template <typename F, typename Shape, typename GPUBuffer>
		static typename detail::bulk_execute_result<F, Shape>::type
		bulk_execute(F && f, Shape const& shape, GPUBuffer & buffer)
		{
			/**
			 * The elements of pair are:
			 * begin at array, # of elements to process
			 */
			for(auto const & elem : shape) {
				std::size_t x = elem.first;
				std::size_t y = elem.second;
				F _f( std::move(f) );

				buffer.queue.submit( [_f, &buffer, x, y](cl::sycl::handler & cgh) {
					auto sycl_buffer = buffer.buffer.get();
					auto buffer_view = (*sycl_buffer).template get_access<cl::sycl::access::mode::read_write>(cgh);
					buffer._buffer_view = &buffer_view;

					cgh.parallel_for<class hpx_foreach>(cl::sycl::range<1>(y),
						[=] (cl::sycl::id<1> index)
						{
							auto _x = std::make_pair(x + index[0], 1);
							_f(_x);
						});
				});
			}
			
		}

        std::size_t os_thread_count()
        {
            return 1;
        }
        /// \endcond
    };

    namespace detail
    {
        /// \cond NOINTERNAL
        template <>
        struct is_executor<gpu_sycl_executor>
          : std::true_type
        {};
        /// \endcond
    }
}}}

#endif