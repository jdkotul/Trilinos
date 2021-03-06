#ifndef __TACHOEXP_TASKFUNCTOR_FACTORIZE_CHOL_PANEL_HPP__
#define __TACHOEXP_TASKFUNCTOR_FACTORIZE_CHOL_PANEL_HPP__

/// \file TachoExp_TaskFunctor_FactorizeCholPanel.hpp
/// \author Kyungjoo Kim (kyukim@sandia.gov)

#include "TachoExp_Util.hpp"

#include "TachoExp_CholSupernodes.hpp"
#include "TachoExp_CholSupernodes_SerialPanel.hpp"

namespace Tacho {

  namespace Experimental {

    template<typename MatValueType, typename ExecSpace>
    struct TaskFunctor_FactorizeCholPanel {
    public:
      typedef ExecSpace exec_space;

      typedef Kokkos::TaskScheduler<exec_space> sched_type;
      typedef typename sched_type::member_type member_type;

      typedef Kokkos::MemoryPool<exec_space> memory_pool_type;

      typedef int value_type; // functor return type
      typedef Kokkos::Future<int,exec_space> future_type;

      typedef MatValueType mat_value_type; // matrix value type

      typedef SupernodeInfo<mat_value_type,exec_space> supernode_info_type;
      typedef typename supernode_info_type::supernode_type supernode_type;

      typedef Kokkos::pair<ordinal_type,ordinal_type> range_type;

    private:
      sched_type _sched;
      memory_pool_type _bufpool;

      supernode_info_type _info;
      ordinal_type _sid;

      supernode_type _s;

      ordinal_type _nb;

      ordinal_type _state;

    public:
      KOKKOS_INLINE_FUNCTION
      TaskFunctor_FactorizeCholPanel() = delete;

      KOKKOS_INLINE_FUNCTION
      TaskFunctor_FactorizeCholPanel(const sched_type &sched,
                                     const memory_pool_type &bufpool,
                                     const supernode_info_type &info,
                                     const ordinal_type sid,
                                     const ordinal_type nb)
        : _sched(sched),
          _bufpool(bufpool),
          _info(info),
          _sid(sid),
          _s(info.supernodes(sid)),
          _nb(nb),
          _state(0) {}

      KOKKOS_INLINE_FUNCTION
      ordinal_type factorize_internal(member_type &member, const ordinal_type n, const bool final) {
        const size_t bufsize = (_nb*n + _info.max_schur_size)*sizeof(mat_value_type);
        
        mat_value_type *buf = bufsize > 0 ? (mat_value_type*)_bufpool.allocate(bufsize) : NULL;

        if (buf == NULL && bufsize) 
          return -1; // allocation fails
        
        CholSupernodes<Algo::Workflow::SerialPanel>
          ::factorize_recursive_serial(_sched, member, _info, _sid, final, buf, bufsize, _nb);
        
        _bufpool.deallocate(buf, bufsize);

        return 0;
      }

      KOKKOS_INLINE_FUNCTION
      void operator()(member_type &member, value_type &r_val) {
        if (get_team_rank(member) == 0) {
          constexpr ordinal_type done = 2;
          TACHO_TEST_FOR_ABORT(_state == done, "dead lock");

          if (_s.nchildren == 0 && _state == 0) _state = 1;

          switch (_state) {
          case 0: { // tree parallelsim
            if (_info.serial_thres_size > _s.max_decendant_supernode_size) {
              r_val = factorize_internal(member, _s.max_decendant_schur_size, true);

              // allocation fails
              if (r_val) 
                Kokkos::respawn(this, _sched, Kokkos::TaskPriority::Low);
              else
                _state = done;
            } else {
              // allocate dependence array to handle variable number of children schur contributions
              future_type *dep = NULL, depbuf[MaxDependenceSize]; 
              size_t depbuf_size = _s.nchildren > MaxDependenceSize ? _s.nchildren*sizeof(future_type) : 0;
              if (depbuf_size) {
                dep = (future_type*)_sched.memory()->allocate(depbuf_size);
                clear((char*)dep, depbuf_size);
              } else {
                dep = &depbuf[0];
              }
              
              // spawn children tasks and this (their parent) depends on the children tasks
              if (dep != NULL) {
                for (ordinal_type i=0;i<_s.nchildren;++i) {
                  auto f = Kokkos::task_spawn(Kokkos::TaskSingle(_sched, Kokkos::TaskPriority::Regular),
                                              TaskFunctor_FactorizeCholPanel(_sched, _bufpool, _info, _s.children[i], _nb));
                  TACHO_TEST_FOR_ABORT(f.is_null(), "task allocation fails");
                  dep[i] = f;
                }

                // respawn with updating state
                _state = 1;
                Kokkos::respawn(this, Kokkos::when_all(dep, _s.nchildren), Kokkos::TaskPriority::Regular);
                
                if (depbuf_size) {
                  for (ordinal_type i=0;i<_s.nchildren;++i) (dep+i)->~future_type();
                  _sched.memory()->deallocate(dep, depbuf_size);
                }
              } else {
                // fail to allocate depbuf
                Kokkos::respawn(this, _sched, Kokkos::TaskPriority::Regular);
              }
            }
            break;
          }
          case 1: {
            r_val = factorize_internal(member, _s.n - _s.m, false);
            // allocation fails
            if (r_val) 
              Kokkos::respawn(this, _sched, Kokkos::TaskPriority::Low);
            else
              _state = done;
            break;
          }
          }
        }
      }
    };
  }
}

#endif
