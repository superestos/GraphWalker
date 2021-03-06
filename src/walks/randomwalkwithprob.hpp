#ifndef RANDOMWALKWITHSTOP
#define RANDOMWALKWITHSTOP

#include <string>
#include <fstream>
#include <time.h>

#include "walks/walk.hpp" 
#include "api/datatype.hpp"

/**
 * Type definitions. Remember to create suitable graph shards using the
 * Sharder-program.
 */
 
class RandomWalkwithProb : public RandomWalk {

public:  

    void updateByWalk(WalkDataType walk, wid_t walkid, bid_t exec_block, eid_t *&beg_pos, vid_t *&csr, WalkManager &walk_manager ){ //, VertexDataType* vertex_value){
        // logstream(LOG_INFO) << "updateByWalk in randomwalkwithstop." << std::endl;
        tid_t threadid = omp_get_thread_num();
        WalkDataType nowWalk = walk;
        vid_t sourId = walk_manager.getSourceId(nowWalk);
        vid_t dstId = walk_manager.getCurrentId(nowWalk) + blocks[exec_block];
        hid_t hop = walk_manager.getHop(nowWalk);
        unsigned seed = (unsigned)(walkid+dstId+hop+(unsigned)time(NULL));
        // logstream(LOG_DEBUG) << "dstId = " << dstId << ",  exec_block = " << exec_block << ", range = [" << blocks[exec_block] << "," << blocks[exec_block+1] << ")"<< std::endl;
        // logstream(LOG_DEBUG) << "hop = " << hop << ",  maxwalklength = " << maxwalklength << std::endl;
        while (dstId >= blocks[exec_block] && dstId < blocks[exec_block+1] ){
            // std::cout  << " -> " << dstId ;//<< " " << walk_manager.getSourceId(walk) << std::endl;
            updateInfo(sourId, dstId, threadid, hop);
            vid_t dstIdp = dstId - blocks[exec_block];
            eid_t outd = beg_pos[dstIdp+1] - beg_pos[dstIdp];
            if (outd > 0 && (float)rand_r(&seed)/RAND_MAX > 0.15 ){
                eid_t pos = beg_pos[dstIdp] - beg_pos[0] + ((eid_t)rand_r(&seed))%outd;
                // logstream(LOG_DEBUG) << "dstId = " << dstId << ",  pos = " << pos << ", csr[pos] = " << csr[pos] << std::endl;
                dstId = csr[pos];
            }else{
                // if(hop>0) logstream(LOG_DEBUG) << "sourId = " << sourId << ", hop " << hop << std::endl;
                return;
            }
            hop++;
            nowWalk++;
        }
        // if( hop < L ){
            bid_t p = getblock( dstId );
            if(p>=nblocks) return;
            if(p == exec_block){
                logstream(LOG_DEBUG) << "dstId = " << dstId << ", p " << p << std::endl;
                assert(false);
            }
            walk_manager.moveWalk(nowWalk, p, threadid, dstId - blocks[p]);
            walk_manager.setMinStep( p, hop );
            walk_manager.ismodified[p] = true;
        // }
    }

};

#endif