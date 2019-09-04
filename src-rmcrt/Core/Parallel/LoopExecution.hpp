/*
 * The MIT License
 *
 * Copyright (c) 1997-2018 The University of Utah
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

// The purpose of this file is to provide portability between Kokkos and non-Kokkos builds.
// The user should be able to #include this file, and then obtain all the tools needed.
// For example, suppose a user calls a parallel_for loop but Kokkos is NOT provided, this will run the
// functor in a loop and also not use Kokkos views.  If Kokkos is provided, this creates a
// lambda expression and inside that it contains loops over the functor.  Kokkos Views are also used.
// At the moment we seek to only support regular CPU code, Kokkos OpenMP, and CUDA execution spaces,
// though it shouldn't be too difficult to expand it to others.  This doesn't extend it
// to CUDA kernels (without Kokkos), and that can get trickier (block/dim parameters) especially with
// regard to a parallel_reduce (many ways to "reduce" a value and return it back to host memory)

#ifndef UINTAH_HOMEBREW_LOOP_EXECUTION_HPP
#define UINTAH_HOMEBREW_LOOP_EXECUTION_HPP

#include <Core/Parallel/Parallel.h>
#include <Core/Exceptions/InternalError.h>
#include <cstring>
#include <cstddef> // TODO: What is this doing here?
#include <vector> //  Used to manage multiple streams in a task.
#include <algorithm>

#include <sci_defs/cuda_defs.h>
#include <sci_defs/kokkos_defs.h>

using std::max;
using std::min;

#if defined( UINTAH_ENABLE_KOKKOS )
#include <Kokkos_Core.hpp>
  const bool HAVE_KOKKOS = true;
#if !defined( KOKKOS_ENABLE_CUDA )  //This define comes from Kokkos itself.
  //Kokkos GPU is not included in this build.  Create some stub types so these types at least exist.
  namespace Kokkos {
    class Cuda {};
    class CudaSpace {};
  }
#elif !defined( KOKKOS_ENABLE_OPENMP )
  // For the Unified Scheduler + Kokkos GPU but not Kokkos OpenMP.
  // This logic may be temporary if all GPU functionality is merged into the Kokkos scheduler
  // and the Unified Scheduler is no longer used for GPU logic.  Brad P Jun 2018
  namespace Kokkos {
    class OpenMP {};
  }
#endif
#else //if defined( UINTAH_ENABLE_KOKKOS )

  //Kokkos not included in this build.  Create some stub types so these types at least exist.
  namespace Kokkos {
    class OpenMP {};
    class HostSpace {};
    class Cuda {};
    class CudaSpace {};
  }
  const bool HAVE_KOKKOS = false;
#define KOKKOS_LAMBDA [&]

#endif //UINTAH_ENABLE_KOKKOS

// Create some data types for non-Kokkos CPU runs.
namespace UintahSpaces{
  class CPU {};
  class HostSpace {};
}

enum TASKGRAPH {
  DEFAULT = -1
};
// Macros don't like passing in data types that contain commas in them,
// such as two template arguments. This helps fix that.
#define COMMA ,

// Helps turn defines into usable strings (even if it has a comma in it)
#define STRV(...) #__VA_ARGS__
#define STRVX(...) STRV(__VA_ARGS__)

// Boilerplate alert.  This can be made much cleaner in C++17 with compile time if statements (if constexpr() logic.)
// We would be able to use the following "using" statements instead of the "#defines", and do compile time comparisons like so:
// if constexpr  (std::is_same< Kokkos::Cuda,      TAG1 >::value) {
// For C++11, the process is doable but just requires a muck of boilerplating to get there, instead of the above if
// statement, I instead opted for a weirder system where I compared the tags as strings
// if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG1)) == 0) {
// Further, the C++11 way ended up defining a tag as more of a string of code rather than an actual data type.

//using UINTAH_CPU_TAG = UintahSpaces::CPU;
//using KOKKOS_OPENMP_TAG = Kokkos::OpenMP;
//using KOKKOS_CUDA_TAG = Kokkos::Cuda;

// Main concept of the below tags: Whatever tags the user supplies is directly compiled into the Uintah binary build.
// In case of a situation where a user supplies a tag that isn't valid for that build, such as KOKKOS_CUDA_TAG in a non-CUDA build,
// the tag is "downgraded" to one that is valid.  So in a non-CUDA build, KOKKOS_CUDA_TAG gets associated with
// Kokkos::OpenMP or UintahSpaces::CPU.  This helps ensure that the compiler never attempts to compile anything with a
// Kokkos::Cuda data type in a non-GPU build
#define ORIGINAL_UINTAH_CPU_TAG     UintahSpaces::CPU COMMA UintahSpaces::HostSpace
#define ORIGINAL_KOKKOS_OPENMP_TAG  Kokkos::OpenMP COMMA Kokkos::HostSpace
#define ORIGINAL_KOKKOS_CUDA_TAG    Kokkos::Cuda COMMA Kokkos::CudaSpace

#if defined(UINTAH_ENABLE_KOKKOS) && defined(HAVE_CUDA)
  #define UINTAH_CPU_TAG              UintahSpaces::CPU COMMA UintahSpaces::HostSpace
  #if defined(KOKKOS_ENABLE_OPENMP)
    #define KOKKOS_OPENMP_TAG         Kokkos::OpenMP COMMA Kokkos::HostSpace
  #else
    #define KOKKOS_OPENMP_TAG         UintahSpaces::CPU COMMA UintahSpaces::HostSpace
  #endif
  #define KOKKOS_CUDA_TAG             Kokkos::Cuda COMMA Kokkos::CudaSpace
#elif defined(UINTAH_ENABLE_KOKKOS) && !defined(HAVE_CUDA)
  #define UINTAH_CPU_TAG              UintahSpaces::CPU COMMA UintahSpaces::HostSpace
  #if defined(KOKKOS_ENABLE_OPENMP)
    #define KOKKOS_OPENMP_TAG         Kokkos::OpenMP COMMA Kokkos::HostSpace
    #define KOKKOS_CUDA_TAG           Kokkos::OpenMP COMMA Kokkos::HostSpace
  #else
    #define KOKKOS_OPENMP_TAG         UintahSpaces::CPU COMMA UintahSpaces::HostSpace
    #define KOKKOS_CUDA_TAG           UintahSpaces::CPU COMMA UintahSpaces::HostSpace
  #endif
#elif !defined(UINTAH_ENABLE_KOKKOS)
  #define UINTAH_CPU_TAG              UintahSpaces::CPU COMMA UintahSpaces::HostSpace
  #define KOKKOS_OPENMP_TAG           UintahSpaces::CPU COMMA UintahSpaces::HostSpace
  #define KOKKOS_CUDA_TAG             UintahSpaces::CPU COMMA UintahSpaces::HostSpace
#endif


enum TaskAssignedExecutionSpace {
  NONE_SPACE = 0,
  UINTAH_CPU = 1,                          //binary 001
  KOKKOS_OPENMP = 2,                       //binary 010
  KOKKOS_CUDA = 4,                         //binary 100
};


// Helps turn on runtime specific execution flags
#define PREPARE_UINTAH_CPU_TASK(TASK) {                                                            \
}

#define PREPARE_KOKKOS_OPENMP_TASK(TASK) {                                                         \
  TASK->usesKokkosOpenMP(true);                                                                    \
}

#define PREPARE_KOKKOS_CUDA_TASK(TASK) {                                                           \
  TASK->usesDevice(true);                                                                          \
  TASK->usesKokkosCuda(true);                                                                      \
  TASK->usesSimVarPreloading(true);                                                                \
}
// This macro gives a mechanism to allow the user to do two things.
// 1) Specify all execution spaces allowed by this task
// 2) Generate task objects and task object options.
//    At compile time, the compiler will compile the task for all specified execution spaces.
//    At run time, the appropriate if statement logic will determine which task to use.
// TAG1, TAG2, and TAG3 are possible execution spaces this task supports.
// TASK_DEPENDENCIES are a functor which performs all additional task specific options the user desires
// FUNCTION_NAME is the string name of the task.
// FUNCTION_CODE_NAME is the function pointer without the template arguments, and this macro
// tacks on the appropriate template arguments.  (This is the major reason why a macro is used.)
// PATCHES and MATERIALS are normal Task object arguments.
// ... are additional variatic Task arguments

// Logic note, we don't currently allow both a Uintah CPU task and a Kokkos CPU task to exist in the same
// compiled build (thought it wouldn't be hard to implement it).  But we do allow a Kokkos CPU and
// Kokkos GPU task to exist in the same build
#define CALL_ASSIGN_PORTABLE_TASK_3TAGS(TAG1, TAG2, TAG3,                                          \
                                  TASK_DEPENDENCIES,                                               \
                                  FUNCTION_NAME, FUNCTION_CODE_NAME,                               \
                                  PATCHES, MATERIALS,                                              \
                                  TASK_GRAPH_ID,                                                   \
                                  ...) {                                                           \
  Task* task{nullptr};                                                                             \
                                                                                                   \
  if (Uintah::Parallel::usingDevice()) {                                                           \
    if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG1)) == 0) {                               \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_CUDA_TASK(task);                                                              \
    } else if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG2)) == 0) {                        \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG2>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_CUDA_TASK(task);                                                              \
    } else if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG3)) == 0) {                        \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG3>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_CUDA_TASK(task);                                                              \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  if (!task) {                                                                                     \
    if (strcmp(STRVX(ORIGINAL_KOKKOS_OPENMP_TAG), STRVX(TAG1)) == 0) {                             \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_OPENMP_TASK(task);                                                            \
    } else if (strcmp(STRVX(ORIGINAL_KOKKOS_OPENMP_TAG), STRVX(TAG2)) == 0) {                      \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG2>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_OPENMP_TASK(task);                                                            \
    } else if (strcmp(STRVX(ORIGINAL_KOKKOS_OPENMP_TAG), STRVX(TAG3)) == 0) {                      \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG3>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_OPENMP_TASK(task);                                                            \
    } else if (strcmp(STRVX(ORIGINAL_UINTAH_CPU_TAG), STRVX(TAG1)) == 0) {                         \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_UINTAH_CPU_TASK(task);                                                               \
    } else if (strcmp(STRVX(ORIGINAL_UINTAH_CPU_TAG), STRVX(TAG2)) == 0) {                         \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG2>, ## __VA_ARGS__);          \
      PREPARE_UINTAH_CPU_TASK(task);                                                               \
    } else if (strcmp(STRVX(ORIGINAL_UINTAH_CPU_TAG), STRVX(TAG3)) == 0) {                         \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG3>, ## __VA_ARGS__);          \
      PREPARE_UINTAH_CPU_TASK(task);                                                               \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  if (task) {                                                                                      \
    TASK_DEPENDENCIES(task);                                                                       \
  }                                                                                                \
                                                                                                   \
  if (task) {                                                                                      \
    sched->addTask(task, PATCHES, MATERIALS, TASK_GRAPH_ID);                                       \
  }                                                                                                \
}



//If only 2 execution space tags are specified
#define CALL_ASSIGN_PORTABLE_TASK_2TAGS(TAG1, TAG2,                                                \
                                  TASK_DEPENDENCIES,                                               \
                                  FUNCTION_NAME, FUNCTION_CODE_NAME,                               \
                                  PATCHES, MATERIALS,                                              \
                                  TASK_GRAPH_ID,                                                   \
                                  ...) {                                                           \
  Task* task{nullptr};                                                                             \
                                                                                                   \
  if (Uintah::Parallel::usingDevice()) {                                                           \
    if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG1)) == 0) {                               \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_CUDA_TASK(task);                                                              \
    } else if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG2)) == 0) {                        \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG2>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_CUDA_TASK(task);                                                              \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  if (!task) {                                                                                     \
    if (strcmp(STRVX(ORIGINAL_KOKKOS_OPENMP_TAG), STRVX(TAG1)) == 0) {                             \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_OPENMP_TASK(task);                                                            \
    } else if (strcmp(STRVX(ORIGINAL_KOKKOS_OPENMP_TAG), STRVX(TAG2)) == 0) {                      \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG2>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_OPENMP_TASK(task);                                                            \
    } else if (strcmp(STRVX(ORIGINAL_UINTAH_CPU_TAG), STRVX(TAG1)) == 0) {                         \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_UINTAH_CPU_TASK(task);                                                               \
    } else if (strcmp(STRVX(ORIGINAL_UINTAH_CPU_TAG), STRVX(TAG2)) == 0) {                         \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG2>, ## __VA_ARGS__);          \
      PREPARE_UINTAH_CPU_TASK(task);                                                               \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  if (task) {                                                                                      \
    TASK_DEPENDENCIES(task);                                                                       \
  }                                                                                                \
                                                                                                   \
  if (task) {                                                                                      \
    sched->addTask(task, PATCHES, MATERIALS, TASK_GRAPH_ID);                                       \
  }                                                                                                \
}
//If only 1 execution space tag is specified
#define CALL_ASSIGN_PORTABLE_TASK_1TAG(TAG1,                                                       \
                                  TASK_DEPENDENCIES,                                               \
                                  FUNCTION_NAME, FUNCTION_CODE_NAME,                               \
                                  PATCHES, MATERIALS,                                              \
                                  TASK_GRAPH_ID,                                                   \
                                  ...) {                                                           \
  Task* task{nullptr};                                                                             \
                                                                                                   \
  if (Uintah::Parallel::usingDevice()) {                                                           \
    if (strcmp(STRVX(ORIGINAL_KOKKOS_CUDA_TAG), STRVX(TAG1)) == 0) {                               \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_CUDA_TASK(task);                                                              \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  if (!task) {                                                                                     \
    if (strcmp(STRVX(ORIGINAL_KOKKOS_OPENMP_TAG), STRVX(TAG1)) == 0) {                             \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_KOKKOS_OPENMP_TASK(task);                                                            \
    } else if (strcmp(STRVX(ORIGINAL_UINTAH_CPU_TAG), STRVX(TAG1)) == 0) {                         \
      task = scinew Task(FUNCTION_NAME, this, &FUNCTION_CODE_NAME TAG1>, ## __VA_ARGS__);          \
      PREPARE_UINTAH_CPU_TASK(task);                                                               \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  if (task) {                                                                                      \
    TASK_DEPENDENCIES(task);                                                                       \
  }                                                                                                \
                                                                                                   \
  if (task) {                                                                                      \
    sched->addTask(task, PATCHES, MATERIALS, TASK_GRAPH_ID);                                       \
  }                                                                                                \
}

namespace Uintah {

class BlockRange;

class BlockRange
{
public:

  enum { rank = 3 };

  BlockRange(){}

  template <typename ArrayType>
  void setValues( ArrayType const & c0, ArrayType const & c1 )
  {
    for (int i=0; i<rank; ++i) {
      m_offset[i] = c0[i] < c1[i] ? c0[i] : c1[i];
      m_dim[i] =   (c0[i] < c1[i] ? c1[i] : c0[i]) - m_offset[i];
    }
  }

  template <typename ArrayType>
  BlockRange( ArrayType const & c0, ArrayType const & c1 )
  {
    setValues( c0, c1 );
  }

  template <typename ArrayType>
  BlockRange(void* stream, ArrayType const & c0, ArrayType const & c1 )
  {
    setValues( stream, c0, c1 );
  }

  template <typename ArrayType>
  BlockRange(const std::vector<void*>& streams, ArrayType const & c0, ArrayType const & c1 )
  {
    setValues( streams, c0, c1 );
  }

  BlockRange( const BlockRange& obj ) {
    for (int i=0; i<rank; ++i) {
      this->m_offset[i] = obj.m_offset[i];
      this->m_dim[i] = obj.m_dim[i];
    }
  }

  template <typename ArrayType>
  void setValues(void* stream, ArrayType const & c0, ArrayType const & c1 )
  {
    for (int i=0; i<rank; ++i) {
      m_offset[i] = c0[i] < c1[i] ? c0[i] : c1[i];
      m_dim[i] =   (c0[i] < c1[i] ? c1[i] : c0[i]) - m_offset[i];
    }
#ifdef HAVE_CUDA
    m_streams.push_back(stream);
#endif //Ignore the non-CUDA case as those streams are pointless.
  }

  template <typename ArrayType>
  void setValues(const std::vector<void*>& streams, ArrayType const & c0, ArrayType const & c1 )
  {
    for (int i=0; i<rank; ++i) {
      m_offset[i] = c0[i] < c1[i] ? c0[i] : c1[i];
      m_dim[i] =   (c0[i] < c1[i] ? c1[i] : c0[i]) - m_offset[i];
    }
#ifdef HAVE_CUDA
    for (auto& stream : streams) {
      m_streams.push_back(stream);
    }
#endif //Ignore the non-CUDA case as those streams are pointless.
  }

  int begin( int r ) const { return m_offset[r]; }
  int   end( int r ) const { return m_offset[r] + m_dim[r]; }

  size_t size() const
  {
    size_t result = 1u;
    for (int i=0; i<rank; ++i) {
      result *= m_dim[i];
    }
    return result;
  }

private:
  int m_offset[rank];
  int m_dim[rank];
public:
  void * getStream() const {
    if ( m_streams.size() == 0 ) {
      return nullptr;
    } else {
      return m_streams[0];
    }
  }
  void * getStream(unsigned int i) const {
    if ( i >= m_streams.size() ) {
      SCI_THROW(InternalError("Requested a stream that doesn't exist.", __FILE__, __LINE__));
    } else {
      return m_streams[i];
    }
  }

  unsigned int getNumStreams() const {
    return m_streams.size();
  }

private:
  std::vector<void*> m_streams;
};

//----------------------------------------------------------------------------
// Start parallel loops
//----------------------------------------------------------------------------


// -------------------------------------  parallel_for loops  ---------------------------------------------
#if defined(UINTAH_ENABLE_KOKKOS)
template <typename ExecutionSpace, typename Functor>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
parallel_for( BlockRange const & r, const Functor & functor )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  // Note, capturing with [&] generated a compiler bug when also using the nvcc_wrapper.
  // But capturing with [=] worked fine, no compiler bugs.
  Kokkos::parallel_for( Kokkos::RangePolicy<Kokkos::OpenMP, int>(kb, ke).set_chunk_size(2), [=](int k) {
    for (int j=jb; j<je; ++j) {
    for (int i=ib; i<ie; ++i) {
      functor(i,j,k);
    }}
  });
}
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

#if defined(UINTAH_ENABLE_KOKKOS)
#if defined(HAVE_CUDA)
template <typename ExecutionSpace, typename Functor>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
parallel_for( BlockRange const & r, const Functor & functor )
{

  // Team policy approach (reuses CUDA threads)

  // Overall goal, split a 3D range requested by the user into various SMs on the GPU.  (In essence, this would be a Kokkos MD_Team+Policy, if one existed)
  // The process requires going from 3D range to a 1D range, partitioning the 1D range into groups that are multiples of 32,
  // then converting that group of 32 range back into a 3D (i,j,k) index.
  unsigned int i_size = r.end(0) - r.begin(0);
  unsigned int j_size = r.end(1) - r.begin(1);
  unsigned int k_size = r.end(2) - r.begin(2);
  unsigned int rbegin0 = r.begin(0);
  unsigned int rbegin1 = r.begin(1);
  unsigned int rbegin2 = r.begin(2);

  // The user has two partitions available.  1) One is the total number of streaming multiprocessors.  2) The other is
  // splitting a task into multiple streams and execution units.

  const unsigned int numItems = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);

  // Get the requested amount of threads per streaming multiprocessor (SM) and number of SMs totals.
  const unsigned int cuda_threads_per_sm   = Uintah::Parallel::getCudaThreadsPerSM();
  const unsigned int cuda_sms_per_loop     = Uintah::Parallel::getCudaSMsPerLoop();
  const unsigned int streamPartitions = r.getNumStreams();

  // The requested range of data may not have enough work for the requested command line arguments, so shrink them if necessary.
  const unsigned int actual_threads = (numItems / streamPartitions) > (cuda_threads_per_sm * cuda_sms_per_loop)
                                    ? (cuda_threads_per_sm * cuda_sms_per_loop) : (numItems / streamPartitions);
  const unsigned int actual_threads_per_sm = (numItems / streamPartitions) > cuda_threads_per_sm ? cuda_threads_per_sm : (numItems / streamPartitions);
  const unsigned int actual_cuda_sms_per_loop = (actual_threads - 1) / cuda_threads_per_sm + 1;

  for (unsigned int i = 0; i < streamPartitions; i++) {
    void* stream = r.getStream(i);
    if (!stream) {
      std::cout << "Error, the CUDA stream must not be nullptr\n" << std::endl;
      SCI_THROW(InternalError("Error, the CUDA stream must not be nullptr.", __FILE__, __LINE__));
    }
    Kokkos::Cuda instanceObject(*(static_cast<cudaStream_t*>(stream)));

    // Use a Team Policy, this allows us to control how many threads per SM and how many SMs are used.
    typedef Kokkos::TeamPolicy< Kokkos::Cuda > policy_type;
    Kokkos::TeamPolicy< Kokkos::Cuda > tp( instanceObject, actual_cuda_sms_per_loop, actual_threads_per_sm );
    Kokkos::parallel_for ( tp, [=] __device__ ( typename policy_type::member_type thread ) {

      // We are within an SM, and all SMs share the same amount of assigned CUDA threads.
      // Figure out which range of N items this SM should work on (as a multiple of 32).
      const unsigned int currentPartition = i * actual_cuda_sms_per_loop + thread.league_rank();
      unsigned int estimatedThreadAmount = numItems * (currentPartition) / ( actual_cuda_sms_per_loop * streamPartitions );
      const unsigned int startingN =  estimatedThreadAmount + ((estimatedThreadAmount % 32 == 0) ? 0 : (32-estimatedThreadAmount % 32));
      unsigned int endingN;
      // Check if this is the last partition
      if ( currentPartition + 1 == actual_cuda_sms_per_loop * streamPartitions ) {
        endingN = numItems;
      } else {
        estimatedThreadAmount = numItems * ( currentPartition + 1 ) / ( actual_cuda_sms_per_loop * streamPartitions );
        endingN = estimatedThreadAmount + ((estimatedThreadAmount % 32 == 0) ? 0 : (32-estimatedThreadAmount % 32));
      }
      const unsigned int totalN = endingN - startingN;
      //printf("league_rank: %d, team_size: %d, team_rank: %d, startingN: %d, endingN: %d, totalN: %d\n", thread.league_rank(), thread.team_size(), thread.team_rank(), startingN, endingN, totalN);

      Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, totalN), [&, startingN, i_size, j_size, k_size, rbegin0, rbegin1, rbegin2] (const int& N) {
        // Craft an i,j,k out of this range
        //printf("parallel_for team demo - n is %d, league_rank is %d, true n is %d\n", N, thread.league_rank(), (startingN + N));
        const int i = (startingN + N) / (j_size * k_size) + rbegin0;
        const int j = ((startingN + N) / k_size) % j_size + rbegin1;
        const int k = (startingN + N) % k_size + rbegin2;
        // Actually run the functor.
        functor( i, j, k );
      });
    });
  }


// ----------------- Team policy but with one stream -----------------
//  unsigned int i_size = r.end(0) - r.begin(0);
//  unsigned int j_size = r.end(1) - r.begin(1);
//  unsigned int k_size = r.end(2) - r.begin(2);
//  unsigned int rbegin0 = r.begin(0);
//  unsigned int rbegin1 = r.begin(1);
//  unsigned int rbegin2 = r.begin(2);
//
//
//  int cuda_threads_per_sm = Uintah::Parallel::getCudaThreadsPerSM();
//  int cuda_sms_per_loop   = Uintah::Parallel::getCudaSMsPerLoop();
//
//  //If 256 threads aren't needed, use less.
//  //But cap at 256 threads total, as this will correspond to 256 threads in a block.
//  //Later the TeamThreadRange will reuse those 256 threads.  For example, if teamThreadRangeSize is 800, then
//  //Cuda thread 0 will be assigned to n = 0, n = 256, n = 512, and n = 768,
//  //Cuda thread 1 will be assigned to n = 1, n = 257, n = 513, and n = 769...
//
//  const int teamThreadRangeSize = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);
//  const int actualThreads = teamThreadRangeSize > cuda_threads_per_sm ? cuda_threads_per_sm : teamThreadRangeSize;
//
//  typedef Kokkos::TeamPolicy< ExecutionSpace > policy_type;
//
//  Kokkos::parallel_for (Kokkos::TeamPolicy< ExecutionSpace >( cuda_sms_per_loop, actualThreads ),
//                           [=] __device__ ( typename policy_type::member_type thread ) {
//    Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, teamThreadRangeSize), [=] (const int& n) {
//
//      const int i = n / (j_size * k_size) + rbegin0;
//      const int j = (n / k_size) % j_size + rbegin1;
//      const int k = n % k_size + rbegin2;
//      functor( i, j, k );
//    });
//  });

  // ----------------- Range policy with one stream -----------------
//  const int teamThreadRangeSize = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);
//  const int threadsPerGroup = 256;
//  const int actualThreads = teamThreadRangeSize > threadsPerGroup ? threadsPerGroup : teamThreadRangeSize;
//
//  void* stream = r.getStream();
//
//  if (!stream) {
//    std::cout << "Error, the CUDA stream must not be nullptr\n" << std::endl;
//    exit(-1);
//  }
//  Kokkos::Cuda instanceObject(*(static_cast<cudaStream_t*>(stream)));
//  Kokkos::RangePolicy<Kokkos::Cuda> rangepolicy( instanceObject, 0, actualThreads);
//
//  Kokkos::parallel_for( rangepolicy, KOKKOS_LAMBDA ( const int& n ) {
//    int threadNum = n;
//    while ( threadNum < teamThreadRangeSize ) {
//      const int i = threadNum / (j_size * k_size) + rbegin0;
//      const int j = (threadNum / k_size) % j_size + rbegin1;
//      const int k = threadNum % k_size + rbegin2;
//      functor( i, j, k );
//      threadNum += threadsPerGroup;
//    }
//  });
}
#endif  //#if defined(HAVE_CUDA)
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

template <typename ExecutionSpace, typename Functor>
inline typename std::enable_if<std::is_same<ExecutionSpace, UintahSpaces::CPU>::value, void>::type
parallel_for( BlockRange const & r, const Functor & functor )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  for (int k=kb; k<ke; ++k) {
  for (int j=jb; j<je; ++j) {
  for (int i=ib; i<ie; ++i) {
    functor(i,j,k);
  }}}
}

//For legacy loops where no execution space was specified as a template parameter.
template < typename Functor>
void
parallel_for( BlockRange const & r, const Functor & functor )
{
  //Force users into using a single CPU thread if they didn't specify OpenMP
  parallel_for<UintahSpaces::CPU>( r, functor );
}

// -------------------------------------  parallel_reduce_sum loops  ---------------------------------------------

#if defined(UINTAH_ENABLE_KOKKOS)
template <typename ExecutionSpace, typename Functor, typename ReductionType>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
parallel_reduce_sum( BlockRange const & r, const Functor & functor, ReductionType & red  )
{
  ReductionType tmp = red;
  unsigned int i_size = r.end(0) - r.begin(0);
  unsigned int j_size = r.end(1) - r.begin(1);
  unsigned int k_size = r.end(2) - r.begin(2);
  unsigned int rbegin0 = r.begin(0);
  unsigned int rbegin1 = r.begin(1);
  unsigned int rbegin2 = r.begin(2);

  // MDRange approach
  //    typedef typename Kokkos::MDRangePolicy<ExecutionSpace, Kokkos::Rank<3,
  //                                                         Kokkos::Iterate::Left,
  //                                                         Kokkos::Iterate::Left>
  //                                                         > MDPolicyType_3D;
  //
  //    MDPolicyType_3D mdpolicy_3d( {{r.begin(0),r.begin(1),r.begin(2)}}, {{r.end(0),r.end(1),r.end(2)}} );
  //
  //    Kokkos::parallel_reduce( mdpolicy_3d, f, tmp );


  // Manual approach
  //    const int ib = r.begin(0); const int ie = r.end(0);
  //    const int jb = r.begin(1); const int je = r.end(1);
  //    const int kb = r.begin(2); const int ke = r.end(2);
  //
  //    Kokkos::parallel_reduce( Kokkos::RangePolicy<ExecutionSpace, int>(kb, ke).set_chunk_size(2), KOKKOS_LAMBDA(int k, ReductionType & tmp) {
  //      for (int j=jb; j<je; ++j) {
  //      for (int i=ib; i<ie; ++i) {
  //        f(i,j,k,tmp);
  //      }}
  //    });

  // Team Policy approach
//  const unsigned int teamThreadRangeSize = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);
//
//  const unsigned int actualThreads = teamThreadRangeSize > 16 ? 16 : teamThreadRangeSize;
//
//  typedef Kokkos::TeamPolicy< Kokkos::OpenMP > policy_type;
//
//  Kokkos::parallel_reduce (Kokkos::TeamPolicy< Kokkos::OpenMP >( 1, actualThreads ),
//                           [&] ( typename policy_type::member_type thread, ReductionType& inner_sum ) {
//    //printf("i is %d\n", thread.team_rank());
//
//    Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, teamThreadRangeSize), [&] (const int& n) {
//      const int i = n / (j_size * k_size) + rbegin0;
//      const int j = (n / k_size) % j_size + rbegin1;
//      const int k = n % k_size + rbegin2;
//      functor(i, j, k, inner_sum);
//    });
//  }, tmp);

  //Range policy manual approach:
  const int teamThreadRangeSize = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);
  const int actualThreads = teamThreadRangeSize > 256 ? 256 : teamThreadRangeSize;

  Kokkos::RangePolicy<ExecutionSpace> rangepolicy(0, actualThreads);

  Kokkos::parallel_reduce( rangepolicy, [&, teamThreadRangeSize, j_size, k_size, rbegin0, rbegin1, rbegin2](const int& n, ReductionType & tmp) {
    int threadNum = n;
    while ( threadNum < teamThreadRangeSize ) {
      const int i = threadNum / (j_size * k_size) + rbegin0;
      const int j = (threadNum / k_size) % j_size + rbegin1;
      const int k = threadNum % k_size + rbegin2;
      functor( i, j, k, tmp );
      threadNum += 256;
    }
  }, tmp);

  red = tmp;

}
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

#if defined(UINTAH_ENABLE_KOKKOS)
#if defined(HAVE_CUDA)
template <typename ExecutionSpace, typename Functor, typename ReductionType>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
parallel_reduce_sum( BlockRange const & r, const Functor & functor, ReductionType & red  )
{
  // Overall goal, split a 3D range requested by the user into various SMs on the GPU.  (In essence, this would be a Kokkos MD_Team+Policy, if one existed)
  // The process requires going from 3D range to a 1D range, partitioning the 1D range into groups that are multiples of 32,
  // then converting that group of 32 range back into a 3D (i,j,k) index.
  ReductionType tmp = red;
  unsigned int i_size = r.end(0) - r.begin(0);
  unsigned int j_size = r.end(1) - r.begin(1);
  unsigned int k_size = r.end(2) - r.begin(2);
  unsigned int rbegin0 = r.begin(0);
  unsigned int rbegin1 = r.begin(1);
  unsigned int rbegin2 = r.begin(2);

  // The user has two partitions available.  1) One is the total number of streaming multiprocessors.  2) The other is
  // splitting a task into multiple streams and execution units.

  const unsigned int numItems = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);

  // Get the requested amount of threads per streaming multiprocessor (SM) and number of SMs totals.
  const unsigned int cuda_threads_per_sm = Uintah::Parallel::getCudaThreadsPerSM();
  const unsigned int cuda_sms_per_loop     = Uintah::Parallel::getCudaSMsPerLoop();
  const unsigned int streamPartitions = r.getNumStreams();

  // The requested range of data may not have enough work for the requested command line arguments, so shrink them if necessary.
  const unsigned int actual_threads = (numItems / streamPartitions) > (cuda_threads_per_sm * cuda_sms_per_loop)
                                    ? (cuda_threads_per_sm * cuda_sms_per_loop) : (numItems / streamPartitions);
  const unsigned int actual_threads_per_sm = (numItems / streamPartitions) > cuda_threads_per_sm ? cuda_threads_per_sm : (numItems / streamPartitions);
  const unsigned int actual_cuda_sms_per_loop = (actual_threads - 1) / cuda_threads_per_sm + 1;
  for (unsigned int i = 0; i < streamPartitions; i++) {
    void* stream = r.getStream(i);
    if (!stream) {
      std::cout << "Error, the CUDA stream must not be nullptr\n" << std::endl;
      SCI_THROW(InternalError("Error, the CUDA stream must not be nullptr.", __FILE__, __LINE__));
    }
    Kokkos::Cuda instanceObject(*(static_cast<cudaStream_t*>(stream)));

    // Use a Team Policy, this allows us to control how many threads per SM and how many SMs are used.
    typedef Kokkos::TeamPolicy< Kokkos::Cuda > policy_type;
    Kokkos::TeamPolicy< Kokkos::Cuda > reduce_tp( instanceObject, actual_cuda_sms_per_loop, actual_threads_per_sm );
    Kokkos::parallel_reduce ( reduce_tp, [=] __device__ ( typename policy_type::member_type thread, ReductionType& inner_sum ) {

      // We are within an SM, and all SMs share the same amount of assigned CUDA threads.
      // Figure out which range of N items this SM should work on (as a multiple of 32).
      const unsigned int currentPartition = i * actual_cuda_sms_per_loop + thread.league_rank();
      unsigned int estimatedThreadAmount = numItems * (currentPartition) / ( actual_cuda_sms_per_loop * streamPartitions );
      const unsigned int startingN =  estimatedThreadAmount + ((estimatedThreadAmount % 32 == 0) ? 0 : (32-estimatedThreadAmount % 32));
      unsigned int endingN;
      // Check if this is the last partition
      if ( currentPartition + 1 == actual_cuda_sms_per_loop * streamPartitions ) {
        endingN = numItems;
      } else {
        estimatedThreadAmount = numItems * ( currentPartition + 1 ) / ( actual_cuda_sms_per_loop * streamPartitions );
        endingN = estimatedThreadAmount + ((estimatedThreadAmount % 32 == 0) ? 0 : (32-estimatedThreadAmount % 32));
      }
      const unsigned int totalN = endingN - startingN;
      //printf("league_rank: %d, team_size: %d, team_rank: %d, startingN: %d, endingN: %d, totalN: %d\n", thread.league_rank(), thread.team_size(), thread.team_rank(), startingN, endingN, totalN);

      Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, totalN), [&, startingN, i_size, j_size, k_size, rbegin0, rbegin1, rbegin2] (const int& N) {
        // Craft an i,j,k out of this range
        //printf("reduce team demo - n is %d, league_rank is %d, true n is %d\n", N, thread.league_rank(), (startingN + N));
        const int i = (startingN + N) / (j_size * k_size) + rbegin0;
        const int j = ((startingN + N) / k_size) % j_size + rbegin1;
        const int k = (startingN + N) % k_size + rbegin2;
        // Actually run the functor.
        functor(i,j,k, inner_sum);
      });
    }, tmp);

    red = tmp;
  }

  //  Manual approach using range policy that shares threads.
//  const int teamThreadRangeSize = (i_size > 0 ? i_size : 1) * (j_size > 0 ? j_size : 1) * (k_size > 0 ? k_size : 1);
//  const int threadsPerGroup = 256;
//  const int actualThreads = teamThreadRangeSize > threadsPerGroup ? threadsPerGroup : teamThreadRangeSize;
//
//  void* stream = r.getStream();
//
//  if (!stream) {
//    std::cout << "Error, the CUDA stream must not be nullptr\n" << std::endl;
//    exit(-1);
//  }
//  Kokkos::Cuda instanceObject(*(static_cast<cudaStream_t*>(stream)));
//  Kokkos::RangePolicy<Kokkos::Cuda> rangepolicy( instanceObject, 0, actualThreads);
//
//  Kokkos::parallel_reduce( rangepolicy, KOKKOS_LAMBDA ( const int& n, ReductionType & inner_tmp ) {
//    int threadNum = n;
//    while ( threadNum < teamThreadRangeSize ) {
//      const int i = threadNum / (j_size * k_size) + rbegin0;
//      const int j = (threadNum / k_size) % j_size + rbegin1;
//      const int k = threadNum % k_size + rbegin2;
//      functor( i, j, k, inner_tmp );
//      threadNum += threadsPerGroup;
//    }
//  }, tmp);
//
//  red = tmp;

}
#endif  //#if defined(HAVE_CUDA)
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

template <typename ExecutionSpace, typename Functor, typename ReductionType>
inline typename std::enable_if<std::is_same<ExecutionSpace, UintahSpaces::CPU>::value, void>::type
parallel_reduce_sum( BlockRange const & r, const Functor & functor, ReductionType & red  )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  ReductionType tmp = red;
  for (int k=kb; k<ke; ++k) {
  for (int j=jb; j<je; ++j) {
  for (int i=ib; i<ie; ++i) {
    functor(i,j,k,tmp);
  }}}
  red = tmp;
}

//For legacy loops where no execution space was specified as a template parameter.
template < typename Functor, typename ReductionType>
void
parallel_reduce_sum( BlockRange const & r, const Functor & functor, ReductionType & red )
{
  //Force users into using a single CPU thread if they didn't specify OpenMP
  parallel_reduce_sum<UintahSpaces::CPU>( r, functor, red );
}

// -------------------------------------  parallel_reduce_min loops  ---------------------------------------------


#if defined(UINTAH_ENABLE_KOKKOS)
template <typename ExecutionSpace, typename Functor, typename ReductionType>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
parallel_reduce_min( BlockRange const & r, const Functor & functor, ReductionType & red  )
{
  ReductionType tmp = red;

  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  // Manual approach
  Kokkos::parallel_reduce( Kokkos::RangePolicy<Kokkos::OpenMP, int>(kb, ke).set_chunk_size(2), [=](int k, ReductionType & tmp) {
    for (int j=jb; j<je; ++j) {
    for (int i=ib; i<ie; ++i) {
      functor(i,j,k,tmp);
    }}
  }, Kokkos::Experimental::Min<ReductionType>(tmp));

  red = tmp;

}
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

#if defined(UINTAH_ENABLE_KOKKOS)
#if defined(HAVE_CUDA)
template <typename ExecutionSpace, typename Functor, typename ReductionType>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
parallel_reduce_min( BlockRange const & r, const Functor & functor, ReductionType & red  )
{

  printf("CUDA version of parallel_reduce_min not yet implemented\n");
  exit(-1);

}
#endif  //#if defined(HAVE_CUDA)
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

//TODO: This appears to not do any "min" on the reduction.
template <typename ExecutionSpace, typename Functor, typename ReductionType>
inline typename std::enable_if<std::is_same<ExecutionSpace, UintahSpaces::CPU>::value, void>::type
parallel_reduce_min( BlockRange const & r, const Functor & functor, ReductionType & red  )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  ReductionType tmp = red;
  for (int k=kb; k<ke; ++k) {
  for (int j=jb; j<je; ++j) {
  for (int i=ib; i<ie; ++i) {
    functor(i,j,k,tmp);
  }}}
  red = tmp;
}

// --------------------------------------  Other loops that should get cleaned up ------------------------------

template <typename Functor>
void serial_for( BlockRange const & r, const Functor & functor )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  for (int k=kb; k<ke; ++k) {
  for (int j=jb; j<je; ++j) {
  for (int i=ib; i<ie; ++i) {
    functor(i,j,k);
  }}}
}

//Runtime code has already started using parallel_for constructs.  These should NOT be executed on
//a GPU.  This function allows a developer to ensure the task only runs on CPU code.  Further, we
//will just run this without the use of Kokkos (this is so GPU builds don't require OpenMP as well).
template <typename Functor>
void parallel_for_cpu_only( BlockRange const & r, const Functor & functor )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  for (int k=kb; k<ke; ++k) {
  for (int j=jb; j<je; ++j) {
  for (int i=ib; i<ie; ++i) {
    functor(i,j,k);
  }}}
}

template <typename Functor, typename Option>
void parallel_for( BlockRange const & r, const Functor & f, const Option& op )
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  for (int k=kb; k<ke; ++k) {
  for (int j=jb; j<je; ++j) {
  for (int i=ib; i<ie; ++i) {
    f(op,i,j,k);
  }}}
};

#if defined( UINTAH_ENABLE_KOKKOS )

//This FunctorBuilder exists because I couldn't go the lambda approach.
//I was running into some conflict with Uintah/nvcc_wrapper/Kokkos/CUDA somewhere.
//So I went the alternative route and built a functor instead of building a lambda.
#if defined( HAVE_CUDA )
template < typename Functor, typename ReductionType >
struct FunctorBuilderReduce {
  //Functor& f = nullptr;

  //std::function is probably a wrong idea, CUDA doesn't support these.
  //std::function<void(int i, int j, int k, ReductionType & red)> f;
  int ib{0};
  int ie{0};
  int jb{0};
  int je{0};

  FunctorBuilderReduce(const BlockRange & r, const Functor & f) {
    ib = r.begin(0);
    ie = r.end(0);
    jb = r.begin(1);
    je = r.end(1);
  }
  void operator()(int k,  ReductionType & red) const {
    //const int ib = r.begin(0); const int ie = r.end(0);
    //const int jb = r.begin(1); const int je = r.end(1);

    for (int j=jb; j<je; ++j) {
      for (int i=ib; i<ie; ++i) {
        f(i,j,k,red);
      }
    }
  }
};
#endif

template <typename Functor, typename ReductionType>
void parallel_reduce_1D( BlockRange const & r, const Functor & f, ReductionType & red  ) {
#if !defined( HAVE_CUDA )
  if ( ! Uintah::Parallel::usingDevice() ) {
    ReductionType tmp = red;
    Kokkos::RangePolicy<Kokkos::OpenMP> rangepolicy(r.begin(0), r.end(0));
    Kokkos::parallel_reduce( rangepolicy, f, tmp );
    red = tmp;
  }
#elif defined( HAVE_CUDA )
  //else {
    //This must be a single dimensional range policy, so use Kokkos::RangePolicy
    ReductionType *tmp;
    cudaMallocHost( (void**)&tmp, sizeof(ReductionType) );

    //No streaming, no launch bounds
    //Kokkos::RangePolicy<Kokkos::Cuda> rangepolicy(r.begin(0), r.end(0));
    //No streaming, launch bounds (512 gave 128 registers, 640 gave 96 registers)
    //Kokkos::RangePolicy<Kokkos::Cuda, Kokkos::LaunchBounds<512,1>> rangepolicy(r.begin(0), r.end(0));

    //Streaming
    Kokkos::Cuda instanceObject = Kokkos::Cuda( *(static_cast<cudaStream_t>(r.getStream())) );
 
    //Streaming, no launch bounds
    //Kokkos::RangePolicy<Kokkos::Cuda> rangepolicy(instanceObject, r.begin(0), r.end(0));
    //Streaming, launch bounds   
    Kokkos::RangePolicy<Kokkos::Cuda, Kokkos::LaunchBounds<512,1>> rangepolicy(instanceObject,  r.begin(0), r.end(0));

    Kokkos::parallel_reduce( rangepolicy, f, *tmp );  //TODO: Don't forget about these reduction values.
  //}
#endif
}


#endif //if defined( UINTAH_ENABLE_KOKKOS )



#if defined(UINTAH_ENABLE_KOKKOS)
template <typename ExecutionSpace, typename Functor>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
sweeping_parallel_for( BlockRange const & r, const Functor & functor, const bool plusX, const bool plusY, const bool plusZ , const int npart)
{
  const int ib = r.begin(0); const int ie = r.end(0);
  const int jb = r.begin(1); const int je = r.end(1);
  const int kb = r.begin(2); const int ke = r.end(2);

  ////////////CUBIC BLOCKS SUPPORTED ONLY/////////////////// 
  // RECTANGLES ARE HARD BUT POSSIBLYE MORE EFFICIENT //////
  // ///////////////////////////////////////////////////////
  const int nPartitionsx=npart; // try to break domain into nxnxn block
  const int nPartitionsy=npart; 
  const int nPartitionsz=npart; 
  const int  dx=ie-ib;
  const int  dy=je-jb;
  const int  dz=ke-kb;
  const int  sdx=dx/nPartitionsx;
  const int  sdy=dy/nPartitionsy;
  const int  sdz=dz/nPartitionsz;
  const int  rdx=dx-sdx*nPartitionsx;
  const int  rdy=dy-sdy*nPartitionsy;
  const int  rdz=dz-sdz*nPartitionsz;



  const int nphase=nPartitionsx+nPartitionsy+nPartitionsz-2;
  int tpp=0; //  Total parallel processes/blocks

  int concurrentBlocksArray[nphase/2+1]; // +1 needed for odd values, use symmetry

  for (int iphase=0; iphase <nphase; iphase++ ){


    if  ((nphase-iphase -1)>= iphase){
      tpp=(iphase+2)*(iphase+1)/2;

      tpp-=max(iphase-nPartitionsx+1,0)*(iphase-nPartitionsx+2)/2;
      tpp-=max(iphase-nPartitionsy+1,0)*(iphase-nPartitionsy+2)/2;
      tpp-=max(iphase-nPartitionsz+1,0)*(iphase-nPartitionsz+2)/2;

      concurrentBlocksArray[iphase]=tpp;
    }else{
      tpp=concurrentBlocksArray[nphase-iphase-1];
    }

    Kokkos::View<int*,  Kokkos::HostSpace> xblock("xblock",tpp) ;
    Kokkos::View<int*,  Kokkos::HostSpace> yblock("yblock",tpp) ;
    Kokkos::View<int*,  Kokkos::HostSpace> zblock("zblock",tpp) ;



    int icount = 0 ;
    for (int k=0;  k< min(iphase+1,nPartitionsz);  k++ ){ // attempts to iterate over k j i , despite  spatial dependencies
      for (int j=0;  j< min(iphase-k+1,nPartitionsy);  j++ ){
        if ((iphase -k-j) <nPartitionsx){
          xblock(icount)=iphase-k-j;
          yblock(icount)=j;
          zblock(icount)=k;
          icount++;
        }
      }
    }

    ///////// Multidirectional parameters
    const int   idir= plusX ? 1 : -1; 
    const int   jdir= plusY ? 1 : -1; 
    const int   kdir= plusZ ? 1 : -1; 
    Kokkos::parallel_for( Kokkos::RangePolicy<Kokkos::OpenMP, int>(0, tpp).set_chunk_size(2), [=](int iblock) {


        const int  xiBlock = plusX ? xblock(iblock) : nPartitionsx-xblock(iblock)-1;  
        const int  yiBlock = plusY ? yblock(iblock) : nPartitionsx-yblock(iblock)-1;
        const int  ziBlock = plusZ ? zblock(iblock) : nPartitionsx-zblock(iblock)-1;

        const int blockx_start=ib+xiBlock *sdx;
        const int blocky_start=jb+yiBlock *sdy;
        const int blockz_start=kb+ziBlock *sdz;

        const int blockx_end= ib+ (xiBlock+1)*sdx +(xiBlock+1 ==nPartitionsx ?  rdx:0 );
        const int blocky_end= jb+ (yiBlock+1)*sdy +(yiBlock+1 ==nPartitionsy ?  rdy:0 );
        const int blockz_end= kb+ (ziBlock+1)*sdz +(ziBlock+1 ==nPartitionsz ?  rdz:0 );

        const int blockx_end_dir= plusX ? blockx_end :-blockx_start+1 ;
        const int blocky_end_dir= plusY ? blocky_end :-blocky_start+1 ;
        const int blockz_end_dir= plusZ ? blockz_end :-blockz_start+1 ;


        for (int k=plusZ? blockz_start : blockz_end-1; k*kdir<blockz_end_dir; k=k+kdir) {
          for (int j=plusY? blocky_start : blocky_end-1; j*jdir<blocky_end_dir; j=j+jdir) {
            for (int i=plusX? blockx_start : blockx_end-1; i*idir<blockx_end_dir; i=i+idir) {
              functor(i,j,k);
            }}}
        });


  }
}
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)

//Allows the user to specify a vector (or view) of indices that require an operation, often needed for boundary conditions and possibly structured grids
#if defined(UINTAH_ENABLE_KOKKOS)
template <typename ExecutionSpace, typename Functor>
inline typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
parallel_for( Kokkos::View<int*, Kokkos::HostSpace> iterSpace, const Functor & functor)
{


    const int n_tot=iterSpace.size()/3;
    Kokkos::parallel_for( Kokkos::RangePolicy<Kokkos::OpenMP, int>(0, n_tot).set_chunk_size(2), [=](int iblock) {
          const int i =  iterSpace[iblock*3];
          const int j =  iterSpace[iblock*3+1];
          const int k =  iterSpace[iblock*3+2];
          functor(i,j,k);
        });

}
#endif  //#if defined(UINTAH_ENABLE_KOKKOS)



//Allows the user to specify a vector (or view) of indices that require an operation, often needed for boundary conditions and possibly structured grids
//This GPU version is mostly a copy of the original GPU version
#if defined(UINTAH_ENABLE_KOKKOS)
#if defined(HAVE_CUDA)
template <typename ExecutionSpace, typename Functor>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
parallel_for(Kokkos::View<int*, Kokkos::HostSpace> iterSpace , const Functor & functor )
{
  const int number_of_indices=3;
  Kokkos::View< int*, Kokkos::CudaSpace >  iterSpace_gpu("BoundaryCondition_uintah_parallel_for_list",iterSpace.size());
  Kokkos::deep_copy(iterSpace_gpu,iterSpace);
  const int n_tot=iterSpace.size()/number_of_indices;



  int cuda_threads_per_sm = Uintah::Parallel::getCudaThreadsPerSM();
  int cuda_sms_per_loop   = Uintah::Parallel::getCudaSMsPerLoop();

  const int actualThreads = n_tot > cuda_threads_per_sm ? cuda_threads_per_sm : n_tot;

  typedef Kokkos::TeamPolicy< ExecutionSpace > policy_type;



  Kokkos::parallel_for (Kokkos::TeamPolicy< ExecutionSpace >( cuda_sms_per_loop, actualThreads ),
                           [=] __device__ ( typename policy_type::member_type thread ) {

    Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, n_tot), [=] (const int& iblock) {
          const int i =  iterSpace_gpu[iblock*number_of_indices];
          const int j =  iterSpace_gpu[iblock*number_of_indices+1];
          const int k =  iterSpace_gpu[iblock*number_of_indices+2];
          functor(i,j,k);
    });
});
}
#endif
#endif

#if defined(UINTAH_ENABLE_KOKKOS)

template <typename ExecutionSpace, typename T2, typename T3>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
    parallel_for(T2 KV3,const T3 init_val ){

    Kokkos::parallel_for( Kokkos::RangePolicy<ExecutionSpace, int>(0,KV3.m_view.size() ).set_chunk_size(2), [=](int i) {
        KV3(i)=init_val;
        });
}


#if defined(HAVE_CUDA)
template <typename ExecutionSpace, typename T2, typename T3>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
parallel_for(T2 KV3,const T3 init_val)
{


  int cuda_threads_per_sm = Uintah::Parallel::getCudaThreadsPerSM();
  int cuda_sms_per_loop   = Uintah::Parallel::getCudaSMsPerLoop();

  const int num_cells=KV3.m_view.size();
  const int actualThreads = num_cells > cuda_threads_per_sm ? cuda_threads_per_sm : num_cells;

  typedef Kokkos::TeamPolicy< ExecutionSpace > policy_type;



  Kokkos::parallel_for (Kokkos::TeamPolicy< ExecutionSpace >( cuda_sms_per_loop, actualThreads ),
      KOKKOS_LAMBDA ( typename policy_type::member_type thread ) {

            Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, num_cells), [=] (const int& i) {
               KV3(i)=init_val;
            });
        });
}
#endif
#endif


#if defined(UINTAH_ENABLE_KOKKOS)
template <typename ExecutionSpace, typename T2, typename T3>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
    parallel_initialize_grouped(T2 KKV3,const T3 init_val ){
        int n_cells=0;
        for (unsigned int j=0; j < KKV3.size(); j++){
          n_cells+=KKV3(j).m_view.size();
        }
    
    Kokkos::parallel_for( Kokkos::RangePolicy<ExecutionSpace, int>(0,n_cells ).set_chunk_size(2), [=](int i_tot) {
        // THis seems like a really bad idea.......
        // Do search to find which index the thread is releavant to.......... yuck, this might be faster on the GPU
        int i=i_tot ;
        int j=0;
        while(i-(int) KKV3(j).m_view.size()>-1){
        i-=KKV3(j).m_view.size();
        j++;
        }
        KKV3(j)(i)=init_val;
        });
}

template <typename ExecutionSpace, typename T2, typename T3>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
    parallel_initialize_single(T2 KKV3,const T3 init_val ){
        for (unsigned int j=0; j < KKV3.size(); j++){
          Kokkos::parallel_for( Kokkos::RangePolicy<ExecutionSpace, int>(0,KKV3(j).m_view.size() ).set_chunk_size(2), [=](int i) {
              KKV3(j)(i)=init_val;
              });
        }
}

#endif



template <class TTT> // Needed for the casting inside of the Variadic template, also allows for nested templating
using Alias = TTT;

template <  typename T, typename MemorySpace>
typename std::enable_if<std::is_same<MemorySpace,UintahSpaces::HostSpace>::value, std::vector<Alias<T> >  >::type
    createContainer(int num_field){
   return std::vector<Alias<T> >(num_field); // perform deep copy   (should be ok since it is an empty CCVariable?)
}

template <  typename T, typename MemorySpace>
typename std::enable_if<std::is_same<MemorySpace,UintahSpaces::HostSpace>::value, std::vector<Alias<T> >  >::type
    createConstContainer(int num_field){
   return std::vector<Alias<T> >(num_field); // perform deep copy (should be ok since it is an empty CCVariable?)
}



#if defined(UINTAH_ENABLE_KOKKOS)
  template<typename T, typename MemorySpace>   //Forward Declaration of KokkosView3
  class KokkosView3; 

template <  typename T, typename MemorySpace>
typename std::enable_if<std::is_same<MemorySpace, Kokkos::HostSpace>::value, Kokkos::View<KokkosView3<T,MemorySpace>* , Kokkos::HostSpace > >::type
    createContainer(int num_field ){
      return Kokkos::View<KokkosView3<T,MemorySpace>* , Kokkos::HostSpace >("a_view_of_KokkosView3s",num_field);
}

template <  typename T, typename MemorySpace>
typename std::enable_if<std::is_same<MemorySpace, Kokkos::HostSpace>::value, Kokkos::View<KokkosView3<const T,MemorySpace>* , Kokkos::HostSpace > >::type
    createConstContainer(int num_field  ){
      return Kokkos::View<KokkosView3<const T,MemorySpace>* , Kokkos::HostSpace >("a_view_of_KokkosView3s_with_const",num_field);
}

#if defined(HAVE_CUDA)
template <  typename T, typename MemorySpace>
typename std::enable_if<std::is_same<MemorySpace, Kokkos::CudaSpace>::value, Kokkos::View<KokkosView3<T,MemorySpace>* , Kokkos::HostSpace > >::type
    createContainer(int num_field ){
      return Kokkos::View<KokkosView3<T,MemorySpace>* , Kokkos::HostSpace >("a_view_of_gpu_KokkosView3s",num_field);
}

template <  typename T, typename MemorySpace>
typename std::enable_if<std::is_same<MemorySpace, Kokkos::CudaSpace>::value, Kokkos::View<KokkosView3<const T,MemorySpace>* , Kokkos::HostSpace > >::type
    createConstContainer(int num_field ){
      return Kokkos::View<KokkosView3<const T,MemorySpace>* , Kokkos::HostSpace >("a_view_of_gpu_KokkosView3s_with_const",num_field);
}




template <typename ExecutionSpace, typename T2, typename T3>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
    parallel_initialize_single(T2 KKV3,const T3 init_val ){

  Kokkos::View< KokkosView3<T3,Kokkos::CudaSpace>* , Kokkos::CudaSpace >  KKV3_gpu("parallel_init_single_gpu",KKV3.size());
  Kokkos::deep_copy(KKV3_gpu,KKV3);

    int cuda_threads_per_sm = Uintah::Parallel::getCudaThreadsPerSM();
    int cuda_sms_per_loop   = Uintah::Parallel::getCudaSMsPerLoop();
  for (unsigned int jx=0; jx < KKV3.size(); jx++){

    const int n_cells=KKV3(jx).m_view.size();


    const int actualThreads = n_cells > cuda_threads_per_sm ? cuda_threads_per_sm : n_cells;
    typedef Kokkos::TeamPolicy< ExecutionSpace > policy_type;



    Kokkos::parallel_for (Kokkos::TeamPolicy< ExecutionSpace >( cuda_sms_per_loop, actualThreads ),
        KOKKOS_LAMBDA ( typename policy_type::member_type thread ) {

        Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, n_cells), [=] (const int& i) {
              KKV3_gpu(jx)(i)=init_val;
          });
        });

  }
}



template <typename ExecutionSpace, typename T2, typename T3>
typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
    parallel_initialize_grouped(T2 KKV3,const T3 init_val ){
        int n_cells=0;
        for (unsigned int j=0; j < KKV3.size(); j++){
          n_cells+=KKV3(j).m_view.size();
        }

    Kokkos::View< KokkosView3<T3,Kokkos::CudaSpace>* , Kokkos::CudaSpace >  KKV3_gpu("parallel_init_grouped_gpu",KKV3.size());
    Kokkos::deep_copy(KKV3_gpu,KKV3);


    int cuda_threads_per_sm = Uintah::Parallel::getCudaThreadsPerSM();
    int cuda_sms_per_loop   = Uintah::Parallel::getCudaSMsPerLoop();

    const int actualThreads = n_cells > cuda_threads_per_sm ? cuda_threads_per_sm : n_cells;
    typedef Kokkos::TeamPolicy< ExecutionSpace > policy_type;



    Kokkos::parallel_for (Kokkos::TeamPolicy< ExecutionSpace >( cuda_sms_per_loop, actualThreads ),
        KOKKOS_LAMBDA ( typename policy_type::member_type thread ) {

        Kokkos::parallel_for (Kokkos::TeamThreadRange(thread, n_cells), [=] (const int& i_tot) {
        int i=i_tot ;
        int j=0;
        while(i-(int) KKV3_gpu(j).m_view.size()>-1){ // warp divergence problem????
        i-=KKV3_gpu(j).m_view.size();
        j++;
        }
        KKV3_gpu(j)(i)=init_val;
          });
        });
}
#endif



template<typename T, typename MemorySpace>
inline void setValueAndReturnView(Kokkos::View<T*, MemorySpace>& V,T x, int &index){
  V(index)=x ;
 index++;
 return ;
}

template<typename T, typename MemorySpace>
inline void setValueAndReturnView(Kokkos::View<T*, MemorySpace>& V,Kokkos::View<T*, MemorySpace> small_v, int &index){
int extra_i= small_v.size();
for(int i=0; i<extra_i; i++){
  V(index+i)=small_v(i);
}
index+=extra_i;
  return ;
}


template<typename T, typename MemorySpace>
inline void sumViewSize(T x, int &index){
 index++;
 return ;
}

template<typename T, typename MemorySpace>
inline void sumViewSize(Kokkos::View<T*, MemorySpace> small_v, int &index){
index+=small_v.size(); 
  return ;
}


// Initialization API that takes both KokkosView3 arguments and View<KokkosView3> arguments. 
// If compiling w/o kokkos it takes CC NC SFCX SFCY SFCZ arguments and std::vector<T> arguments 
   template<typename MemorySpace, typename ExecutionSpace, typename T, class ...Ts>  // Could this be modified to accept grid variables AND containters of grid variables?
   typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::OpenMP>::value, void>::type
   parallel_initialize(T inside_value,  Ts... inputs) { //SFINAE might be appropriate
   int n= 0 ; // Get number of variadic arguments
            Alias<int[]>{( //first part of magic unpacker
              sumViewSize< KokkosView3<T,MemorySpace>, Kokkos::HostSpace>(inputs,n)
             ,0)...,0}; //second part of magic unpacker

        Kokkos::View<KokkosView3<T,MemorySpace>*, Kokkos::HostSpace> inputs_("Parallel_initialize_cpu",n) ;
        int i=0; //iterator counter
            Alias<int[]>{( //first part of magic unpacker
              setValueAndReturnView< KokkosView3<T,MemorySpace>, Kokkos::HostSpace>(inputs_ ,inputs,i)
             ,0)...,0}; //second part of magic unpacker
    parallel_initialize_grouped<ExecutionSpace>(inputs_, inside_value );
    //parallel_initialize_single<ExecutionSpace>(inputs_, inside_value ); // safer version, less ambitious
}


////This function is the same as above,but appears to be necessary due to CCVariable support.....
   template<typename MemorySpace, typename ExecutionSpace, typename T, class ...Ts>  // Could this be modified to accept grid variables AND containters of grid variables?
   typename std::enable_if<std::is_same<ExecutionSpace, Kokkos::Cuda>::value, void>::type
   parallel_initialize(T inside_value,  Ts... inputs) { //SFINAE might be appropriate
   int n= 0; // Get number of variadic arguments
            Alias<int[]>{( //first part of magic unpacker
              sumViewSize< KokkosView3<T,MemorySpace>, Kokkos::HostSpace>(inputs,n)
             ,0)...,0}; //second part of magic unpacker

        Kokkos::View<KokkosView3<T,MemorySpace>*, Kokkos::HostSpace> inputs_("Parallel_initialize_gpu",n) ;
        int i=0; //iterator counter
            Alias<int[]>{( //first part of magic unpacker
              setValueAndReturnView< KokkosView3<T,MemorySpace>, Kokkos::HostSpace>(inputs_ ,inputs,i)
             ,0)...,0}; //second part of magic unpacker
    parallel_initialize_grouped<ExecutionSpace>(inputs_, inside_value );
    //parallel_initialize_single<ExecutionSpace>(inputs_, inside_value ); // safer version, less ambitious
}

#endif

template< typename T, typename T2>
void legacy_initialize(T inside_value, std::vector<T2>& data_fields) {  // for vectors
int nfields=data_fields.size();
for(int i=0;  i< nfields; i++){
data_fields[i].initialize(inside_value);
}
return;
}

template< typename T, typename T2>
void legacy_initialize(T inside_value, T2& data_fields) {  // for stand aloen data fields 
data_fields.initialize(inside_value);
return;
}


template<typename MemorySpace, typename ExecutionSpace, typename T, class ...Ts>
typename std::enable_if<std::is_same<ExecutionSpace, UintahSpaces::CPU>::value, void>::type
parallel_initialize(T inside_value,  Ts... inputs) { //SFINAE might be appropriate
         Alias<int[]>{( //first part of magic unpacker
             //inputs.initialize (inside_value)
             legacy_initialize(inside_value,inputs)
             ,0)...,0}; //second part of magic unpacker
}


} // namespace Uintah

#endif // UINTAH_HOMEBREW_LOOP_EXECUTION_HPP
