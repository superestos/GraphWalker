
#define DYNAMICEDATA 1

#include <string>
#include <fstream>
#include <cmath>
#include <fstream>

#include "api/graphwalker_basic_includes.hpp"
#include "walks/randomwalkwithjump.hpp"
#include "util/toplist.hpp"
#include "util/comperror.hpp"

typedef unsigned VertexDataType;
bool semi_external;

class PageRank : public RandomWalkwithJump{
public:
    unsigned N, R, L;
    VertexDataType **vertex_value;
    std::string basefilename;
    vid_t cur_window_st;

public:

    static std::string filename_vertex_data(std::string basefilename) {
        std::stringstream ss;
        ss << basefilename;
        ss << "_GraphWalker/" << sizeof(VertexDataType) << "B.vvalue";
        return ss.str();
    }

    void initializeApp( unsigned _N, unsigned _R, unsigned _L, float tail, std::string _basefilename ){
        N = _N;
        R = _R; //walks per source
        L = _L;
        basefilename = _basefilename;
        initialVertexValue<unsigned>(N, basefilename);
        initializeRW( R*N, L, tail );
        // for(unsigned i=0; i<4; i++){
        //     used_edges[i] = 0;
        //     total_edges[i] = 0;
        // }
    }

    void startWalksbyApp( WalkManager &walk_manager  ){
        //muti threads to start walks
        logstream(LOG_INFO) << "Start walks ! Total walk number = " << R*N << std::endl;
        unsigned nthreads = get_option_int("execthreads", omp_get_max_threads());
        for( unsigned p = 0; p < nshards; p++ ){
            unsigned cap = R*(intervals[p].second-intervals[p].first)/nthreads + 1;
            for( unsigned t = 0; t < nthreads; t++ )
                walk_manager.pwalks[t][p].reserve(cap);
            walk_manager.minstep[p] = 0;
            walk_manager.walknum[p] = (intervals[p].second-intervals[p].first+1)*R;
            
            omp_set_num_threads(nthreads);
            #pragma omp parallel for schedule(static)
                for( unsigned i = intervals[p].first; i <= intervals[p].second; i++ ){
                    vid_t s = i;
                    vid_t cur = s - intervals[p].first;
                    WalkDataType walk = walk_manager.encode(s, cur, 0);
                    for( unsigned j = 0; j < R; j++ ){
                        walk_manager.pwalks[omp_get_thread_num()][p].push_back(walk);
                    }
                }
            if(!semi_external){
                walk_manager.freshIntervalWalks(p);
            }

        }
        if(semi_external){ 
            vertex_value = new VertexDataType*[nthreads];
            for(unsigned i = 0; i < nthreads; i++){
                vertex_value[i] = new VertexDataType[N];
                memset(vertex_value[i], 0, N*sizeof(VertexDataType));
            }
        }
    }

    void updateInfo(vid_t dstId, unsigned threadid, unsigned hop){
        // if(hop >= L-10)
            vertex_value[threadid][dstId-cur_window_st]++; // #pragma omp critical
        // used_edges[threadid]++;
     }

    /**
     * Called before an execution interval is started.
     */
    void before_exec_interval(unsigned exec_interval, vid_t window_st, vid_t window_en, WalkManager &walk_manager) {
        if(!semi_external){
            /*load walks*/
            walk_manager.readIntervalWalks(exec_interval);

            /*load vertex value*/
            cur_window_st = window_st;
            unsigned  window_len =  window_en -  window_st + 1;
            unsigned nthreads = get_option_int("execthreads", omp_get_max_threads());;
            vertex_value = new VertexDataType*[nthreads];
            for(unsigned t = 0; t < nthreads; t++){
                vertex_value[t] = new VertexDataType[window_len];
                memset(vertex_value[t], 0, window_len*sizeof(VertexDataType));
            }
        }
    }
    
    /**
     * Called after an execution interval has finished.
     */
    // void after_exec_interval(unsigned exec_interval, vid_t window_st, vid_t window_en, WalkManager &walk_manager, Vertex *&vertices) {
    void after_exec_interval(unsigned exec_interval, vid_t window_st, vid_t window_en, WalkManager &walk_manager) {
        walk_manager.walknum[exec_interval] = 0;
		walk_manager.minstep[exec_interval] = 0xfffffff;
        unsigned nthreads = get_option_int("execthreads", omp_get_max_threads());;
        for(unsigned t = 0; t < nthreads; t++)
            walk_manager.pwalks[t][exec_interval].clear();

        for( unsigned p = 0; p < nshards; p++){
            if(p == exec_interval ) continue;
            if(semi_external) walk_manager.walknum[p] = 0;
			for(unsigned t=0;t<nthreads;t++){
				walk_manager.walknum[p] += walk_manager.pwalks[t][p].size();
            }
        }

        if(!semi_external){
            /*write back walks*/
            walk_manager.writeIntervalWalks(exec_interval);

            /*write back vertex value*/
            unsigned  window_len =  window_en -  window_st + 1;
            VertexDataType *vertex_value_sum = (VertexDataType*)malloc(sizeof(VertexDataType)*window_len);
            unsigned f = open(filename_vertex_data(basefilename).c_str(), O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
            assert(f >= 0);
            preada(f, vertex_value_sum, sizeof(VertexDataType)*window_len, sizeof(VertexDataType)*window_st);
            close(f);
            for(unsigned i = 0; i < nthreads; i++){
                for(unsigned j = 0; j < window_len; j++){
                    vertex_value_sum[j] += vertex_value[i][j];
                }
            }
            f = open(filename_vertex_data(basefilename).c_str(), O_WRONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
            assert(f >= 0);
            pwritea(f, vertex_value_sum, sizeof(VertexDataType)*window_len, sizeof(VertexDataType)*window_st);
            close(f);
            free(vertex_value_sum);
            for(unsigned i = 0; i < nthreads; i++)
                free(vertex_value[i]);
            free(vertex_value);
        }

        // for(unsigned i=0; i<window_len; i++ ){
        //     total_edges[0] += vertices[i].outd;
        // }
        // compUtilization();
    }

    void compUtilization(){
        for(int i = 1; i < 4; i++){
            used_edges[0] += used_edges[i];
            total_edges[0] += total_edges[i];
        }
        float utilization = (float)used_edges[0] / (float)total_edges[0];
        logstream(LOG_DEBUG) << "IO utilization = " << utilization << std::endl;
        std::string utilization_filename = basefilename + "_CompError/GraphWalker_pr_utilization.csv";
        std::ofstream utilizationfile;
        utilizationfile.open(utilization_filename.c_str(), std::ofstream::app);
        utilizationfile << total_edges[0] << "\t" << used_edges[0] << "\t" << utilization << "\n" ;
        utilizationfile.close();

        for(unsigned i=0; i<4; i++){
            used_edges[i] = 0;
            total_edges[i] = 0;
        }
    }

};


int main(int argc, const char ** argv) {
    /* GraphChi initialization will read the command line
     arguments and the configuration file. */
    set_argc(argc, argv);
    
    /* Metrics object for keeping track of performance count_invectorers
     and other information. Currently required. */
    metrics m("pagerank");
    
    /* Basic arguments for application */
    std::string filename = get_option_string("file", "../DataSet/LiveJournal/soc-LiveJournal1.txt");  // Base filename
    unsigned nvertices = get_option_int("nvertices", 4847571); // Number of vertices
    unsigned R = get_option_int("R", 1); // Number of steps
    unsigned L = get_option_int("L", 20); // Number of steps per walk
    float tail = get_option_float("tail", 0); // Ratio of stop long tail
    float prob = get_option_float("prob", 0.2); // prob of chose min step
    semi_external = get_option_int("semi_external", 0);
    
    long long shardsize = get_option_long("shardsize", 0); // Size of shard, represented in KB

    /* Detect the number of shards or preprocess an input to create them */
    unsigned nshards = convert_if_notexists(filename, shardsize);

    /* Run */
    PageRank program;
    program.initializeApp( nvertices, R, L, tail, filename );
    graphwalker_engine engine(filename, shardsize, nshards, m);
    engine.run(program, prob);

    if(semi_external){
        unsigned nthreads = get_option_int("execthreads", omp_get_max_threads());;
        for(unsigned t = 1; t < nthreads; t++){
            for(unsigned i = 0; i < nvertices; i++ ){
                program.vertex_value[0][i] += program.vertex_value[t][i];
            }
        }
        unsigned f = open(filename_vertex_data(filename).c_str(), O_WRONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        assert(f >= 0);
        pwritea(f, program.vertex_value[0], sizeof(VertexDataType)*nvertices, 0);
        close(f);
        free(program.vertex_value[0]);
    }
    
    computeError<unsigned>(nvertices, filename, 20, "pr");

    /* Report execution metrics */
    metrics_report(m);
    return 0;
}
