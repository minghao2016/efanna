#ifndef EFANNA_BASE_INDEX_H_
#define EFANNA_BASE_INDEX_H_

#include "general/params.hpp"
#include "general/distance.hpp"
#include "general/matrix.hpp"
#include <boost/dynamic_bitset.hpp>
#include <fstream>
#include <iostream>

//#define BATCH_SIZE 200

namespace efanna{
  struct Point {
        unsigned id;
        float dist;
        bool flag;
        Point () {}
        Point (unsigned i, float d, bool f = true): id(i), dist(d), flag(f) {
        }
        bool operator < (const Point &n) const{
            return this->dist < n.dist;
        }
    };

  typedef std::vector<Point>  Points;
  static inline unsigned InsertIntoKnn (Point *addr, unsigned K, Point nn) {
        // find the location to insert
        unsigned j;
        unsigned i = K;
        while (i > 0) {
            j = i - 1;
            if (addr[j].dist <= nn.dist) break;
            i = j;
        }
        // check for equal ID
        unsigned l = i;
        while (l > 0) {
            j = l - 1;
            if (addr[j].dist < nn.dist) break;
            if (addr[j].id == nn.id) return K + 1;
            l = j;
        }
        // i <= K-1
        j = K;
        while (j > i) {
            addr[j] = addr[j-1];
            --j;
        }
        addr[i] = nn;
        return i;
    }
  struct Neighbor {

        float radius;
        float radiusM;
        Points pool;
        unsigned L;
        unsigned Range;
        bool found;
        std::vector<unsigned> nn_old;
        std::vector<unsigned> nn_new;
        std::vector<unsigned> rnn_old;
        std::vector<unsigned> rnn_new;
        unsigned insert (unsigned id, float dist) {
            if (dist > radius) return pool.size();

            unsigned l = InsertIntoKnn(&pool[0], L, Point(id, dist, true));
            if (l <= L) {
                if (L + 1 < pool.size()) {
                    ++L;
                }
                else {
                    radius = pool[L-1].dist;
                }
            }
            return l;
        }

      };

  template <typename DataType>
  class InitIndex{
  public:
    InitIndex(const Matrix<DataType>& features, const Distance<DataType>* d, const IndexParams& params):
      features_(features),
      distance_(d),
      params_(params)
    {
    }
    virtual ~InitIndex() {};
    virtual void buildTrees(){}

    virtual void buildIndex()
    {
	     buildIndexImpl();
    }
    virtual void buildIndexImpl() = 0;
    virtual void loadIndex(char* filename) = 0;
    virtual void saveIndex(char* filename) = 0;
    virtual void loadTrees(char* filename) = 0;
    virtual void saveTrees(char* filename) = 0;
    virtual void loadGraph(char* filename) = 0;
    virtual void saveGraph(char* filename) = 0;
    void saveResults(char* filename){
       std::ofstream out(filename,std::ios::binary);
       std::vector<std::vector<int>>::iterator i;
//std::cout<<nn_results.size()<<std::endl;
       for(i = nn_results.begin(); i!= nn_results.end(); i++){
         std::vector<int>::iterator j;
         int dim = i->size();
//std::cout<<dim<<std::endl;
         out.write((char*)&dim, sizeof(int));
         for(j = i->begin(); j != i->end(); j++){
           int id = *j;
           out.write((char*)&id, sizeof(int));
         }
       }
       out.close();
    }
SearchParams SP;
    void setSearchParams(int epochs, int init_num, int extend_to,int search_trees, int search_lv){
      SP.search_epoches = epochs;
      SP.search_init_num = init_num;
      if(extend_to>init_num) SP.extend_to = init_num;
      else  SP.extend_to = extend_to;
      SP.search_depth = search_lv;
      SP.tree_num = search_trees;
    }



    void nnExpansion_kgraph(size_t K, const DataType* qNow, std::vector<unsigned int>& pool, std::vector<Point>& results){

          unsigned int base_n = features_.get_rows();
          boost::dynamic_bitset<> tbflag(base_n, false);
          boost::dynamic_bitset<> newflag(base_n, false);

          std::vector<Point> knn(K + SP.extend_to +1);

          int remainder = SP.search_init_num % SP.extend_to;
          int nSeg = SP.search_init_num / SP.extend_to;

          //clock_t s,f;

          int Iter = nSeg;
          if (remainder > 0) Iter++;
          int Jter = SP.extend_to;


          for(int i = 0; i<Iter; i++){
        	  if((remainder > 0) && (i == Iter-1))  Jter=remainder;

              unsigned int L = 0;
              for(int j=0; j <Jter ; j++){
                if(!tbflag.test(pool[i*SP.extend_to+j])){
                  knn[L++].id = pool[i*SP.extend_to+j];
                }
              }

              for (unsigned int k = 0; k < L; ++k) {
                  knn[k].dist = distance_->compare(qNow, features_.get_row(knn[k].id), features_.get_cols());
                  newflag.set(knn[k].id);
              }
              std::sort(knn.begin(), knn.begin() + L);

              //s = clock();
              unsigned int k =  0;
              while (k < L) {
                  unsigned int nk = L;
                  if (newflag.test(knn[k].id)){
                    newflag.reset(knn[k].id);
                    tbflag.set(knn[k].id);
                    typename CandidateHeap::reverse_iterator neighbor = knn_graph[knn[k].id].rbegin();
                    for(size_t nnk = 0;nnk < params_.K && neighbor != knn_graph[knn[k].id].rend(); neighbor++, nnk++){
                      if(tbflag.test(neighbor->row_id))continue;
                      tbflag.set(neighbor->row_id);
                      newflag.set(neighbor->row_id);
                      float dist = distance_->compare(qNow, features_.get_row(neighbor->row_id), features_.get_cols());
                      Point nn(neighbor->row_id, dist);
                      unsigned int r = InsertIntoKnn(&knn[0], L, nn);
                   	  if ( (r <= L) && (L + 1 < knn.size())) ++L;
                      if (r < nk) nk = r;
                    }
                  }
                  if (nk <= k) k = nk;
                  else ++k;
              }
              //f = clock();
              //sum = sum + f-s;


              if (L > K) L = K;
              if (results.empty()) {
                  results.reserve(K + 1);
                  results.resize(L + 1);
                  std::copy(knn.begin(), knn.begin() + L, results.begin());
              }
              else {
                  for (unsigned int l = 0; l < L; ++l) {
                      unsigned r = InsertIntoKnn(&results[0], results.size() - 1, knn[l]);
                      if (r < results.size() /* inserted */ && results.size() < (K + 1)) {
                          results.resize(results.size() + 1);
                      }
                  }
              }
          }
          results.pop_back();
    }

    void nnExpansion(size_t K, const DataType* qNow, std::vector<unsigned int>& pool, std::vector<int>& res){

              unsigned int base_n = features_.get_rows();
              boost::dynamic_bitset<> tbflag(base_n, false);
              boost::dynamic_bitset<> newflag(base_n, false);

              CandidateHeap Candidates;

              int remainder = SP.search_init_num % SP.extend_to;
              int nSeg = SP.search_init_num / SP.extend_to;

              int segIter = nSeg;
              if (remainder > 0) segIter++;
              int Jter = SP.extend_to;

              CandidateHeap Results;

              for(int seg = 0; seg<segIter; seg++){
            	  if((remainder > 0) && (seg == segIter-1))  Jter=remainder;

                  for(int j=0; j <Jter ; j++){
                	  unsigned int nn = pool[seg*SP.extend_to+j];
                	  //if(nn>=base_n) std::cout << "query:" << cur << " Init "<< nn << std::endl;
                      if(!tbflag.test(nn)){
                        newflag.set(nn);
                        Candidate<DataType> c(nn, distance_->compare(qNow, features_.get_row(nn), features_.get_cols()));
                        Candidates.insert(c);
                    }
                  }

                  std::vector<unsigned int> ids;
                  int iter=0;
                  while(iter++ < SP.search_epoches){
                      //the heap is max heap
                      ids.clear();
                      typename CandidateHeap::reverse_iterator it = Candidates.rbegin();
                      for(int j = 0; j < SP.extend_to && it != Candidates.rend(); j++,it++){
                    	//  if(it->row_id>=base_n) std::cout<<"query:"<< cur<<" Judge node  "<<it->row_id<<std::endl;
                        if(newflag.test(it->row_id)){
                          newflag.reset(it->row_id);
                          typename CandidateHeap::reverse_iterator neighbor = knn_graph[it->row_id].rbegin();
                          for(; neighbor != knn_graph[it->row_id].rend(); neighbor++){
                        	//  if(neighbor->row_id>=base_n) std::cout<<"query:"<< cur<<" Judge neighbor  "<<neighbor->row_id<<std::endl;
                            if(tbflag.test(neighbor->row_id))continue;
                            tbflag.set(neighbor->row_id);
                            ids.push_back(neighbor->row_id);
                          }
                        }
                      }
                      for(size_t j = 0; j < ids.size(); j++){
                        Candidate<DataType> c(ids[j], distance_->compare(qNow, features_.get_row(ids[j]), features_.get_cols()) );
                        Candidates.insert(c);
                        newflag.set(ids[j]);
                        if(Candidates.size() > (unsigned int)SP.extend_to)Candidates.erase(Candidates.begin());
                      }
                  }
                  typename CandidateHeap::reverse_iterator it = Candidates.rbegin();
                  for(unsigned int j = 0; j < K && it != Candidates.rend(); j++,it++){
                      Results.insert(*it);
                      if(Results.size() > K)Results.erase(Results.begin());
                  }
              }
              typename CandidateHeap::reverse_iterator it = Results.rbegin();
              for(unsigned int j = 0; j < K && it != Candidates.rend(); j++,it++){
            	  res.push_back(it->row_id);
              }
    }

    virtual void knnSearch(int K, const Matrix<DataType>& query){
      getNeighbors(K,query);
    }
    virtual void getNeighbors(size_t K, const Matrix<DataType>& query) = 0;
    virtual void initGraph() = 0;






    //std::vector<unsigned> Range;
    std::vector<std::vector<Point>  > graph;



    std::vector<Neighbor>  nhoods;
    void join(){
      size_t dim = features_.get_cols();
      for(size_t i = 0; i < nhoods.size(); i++){
        size_t uu = 0;
        nhoods[i].found = false;
        for(size_t newi = 0; newi < nhoods[i].nn_new.size(); newi++){
          for(size_t newj = newi+1; newj < nhoods[i].nn_new.size(); newj++){
            unsigned a = nhoods[i].nn_new[newi];
            unsigned b = nhoods[i].nn_new[newj];
            DataType dist = distance_->compare(
              features_.get_row(a), features_.get_row(b), dim);
            unsigned r = nhoods[a].insert(b,dist);
            if(r < params_.Check_K){uu += 2;}
            nhoods[b].insert(a,dist);
          }

          for(size_t oldj = 0; oldj < nhoods[i].nn_old.size(); oldj++){
            unsigned a = nhoods[i].nn_new[newi];
            unsigned b = nhoods[i].nn_old[oldj];
            DataType dist = distance_->compare(
              features_.get_row(a), features_.get_row(b), dim);
            unsigned r = nhoods[a].insert(b,dist);
            if(r < params_.Check_K){uu += 2;}
            nhoods[b].insert(a,dist);
          }
        }
        nhoods[i].found = uu > 0;
      }

    }

    void update (int paramL) {
      for (size_t i = 0; i < nhoods.size(); i++) {
          nhoods[i].nn_new.clear();
          nhoods[i].nn_old.clear();
          nhoods[i].rnn_new.clear();
          nhoods[i].rnn_old.clear();
          nhoods[i].radius = nhoods[i].pool.back().dist;
      }
      //find longest new
      for(size_t i = 0; i < nhoods.size(); i++){
        if(nhoods[i].found){
          unsigned maxl = nhoods[i].Range + params_.S < nhoods[i].L ? nhoods[i].Range + params_.S : nhoods[i].L;
          unsigned c = 0;
          unsigned l = 0;
          while ((l < maxl) && (c < params_.S)) {
              if (nhoods[i].pool[l].flag) ++c;
              ++l;
          }
          nhoods[i].Range = l;
        }
        nhoods[i].radiusM = nhoods[i].pool[nhoods[i].Range-1].dist;
      }
      for (unsigned n = 0; n < nhoods.size(); ++n) {
          Neighbor &nhood = nhoods[n];
          std::vector<unsigned> &nn_new = nhood.nn_new;
          std::vector<unsigned> &nn_old = nhood.nn_old;
          for (unsigned l = 0; l < nhood.Range; ++l) {
              Point &nn = nhood.pool[l];
              Neighbor &nhood_o = nhoods[nn.id];  // nhood on the other side of the edge
              if (nn.flag) {
                  nn_new.push_back(nn.id);
                  if (nn.dist > nhood_o.radiusM) {
                      nhood_o.rnn_new.push_back(n);
                  }
                  nn.flag = false;
              }
              else {
                  nn_old.push_back(nn.id);
                  if (nn.dist > nhood_o.radiusM) {
                      nhood_o.rnn_old.push_back(n);
                  }
              }
          }
      }
      for (unsigned i = 0; i < nhoods.size(); ++i) {
          std::vector<unsigned> &nn_new = nhoods[i].nn_new;
          std::vector<unsigned> &nn_old = nhoods[i].nn_old;
          std::vector<unsigned> &rnn_new = nhoods[i].rnn_new;
          std::vector<unsigned> &rnn_old = nhoods[i].rnn_old;
          if (paramL && (rnn_new.size() > (unsigned int)paramL)) {
              random_shuffle(rnn_new.begin(), rnn_new.end());
              rnn_new.resize(paramL);
          }
          nn_new.insert(nn_new.end(), rnn_new.begin(), rnn_new.end());
          if (paramL && (rnn_old.size() > (unsigned int)paramL)) {
              random_shuffle(rnn_old.begin(), rnn_old.end());
              rnn_old.resize(paramL);
          }
          nn_old.insert(nn_old.end(), rnn_old.begin(), rnn_old.end());
      }
    }
    void refineGraph(){
      std::cout << " refineGraph" << std::endl;
      int iter = 0;
      clock_t s,f;
      s = clock();unsigned int l=100;
      while(iter++ < params_.build_epoches){
        join();
        update(l);
        f = clock();
        std::cout << "iteration "<< iter << " time: "<< (f-s)*1.0/CLOCKS_PER_SEC<<" seconds"<< std::endl;
      }

      //std::cout << nhoods.size() << std::endl;
      knn_graph.clear();

      for(size_t i = 0; i < nhoods.size(); i++){
        CandidateHeap can;
        for(size_t j = 0; j < params_.K; j++){
          Candidate<DataType> c(nhoods[i].pool[j].id,nhoods[i].pool[j].dist);
          can.insert(c);
        }
        while(can.size()<params_.K){
          unsigned id = rand() % nhoods.size();
          DataType dist = distance_->compare(features_.get_row(i), features_.get_row(id),features_.get_cols());
          Candidate<DataType> c(id, dist);
          can.insert(c);
        }
        knn_graph.push_back(can);
      }
    }
typedef std::set<Candidate<DataType>, std::greater<Candidate<DataType>> > CandidateHeap;
typedef std::vector<unsigned int> IndexVec;
protected:
    const Matrix<DataType> features_;
    const Distance<DataType>* distance_;
    const IndexParams params_;
    std::vector<std::vector<int>> knn_table_gt;
    //std::vector<std::vector<int>> knn_graph;
    std::vector<CandidateHeap> knn_graph;
    std::vector<CandidateHeap> NewCands;
    std::vector<IndexVec> nn_new;
    std::vector<IndexVec> nn_old;
    std::vector<IndexVec> rnn_new;
    std::vector<IndexVec> rnn_old;
    std::vector<std::vector<int>> nn_results;
    DataType* Radius;
  };
#define USING_BASECLASS_SYMBOLS \
    using InitIndex<DataType>::distance_;\
    using InitIndex<DataType>::params_;\
    using InitIndex<DataType>::features_;\
    using InitIndex<DataType>::buildIndex;\
    using InitIndex<DataType>::knn_table_gt;\
    using InitIndex<DataType>::nn_results;\
    using InitIndex<DataType>::saveResults;\
    using InitIndex<DataType>::knn_graph;\
    using InitIndex<DataType>::refineGraph;\
    using InitIndex<DataType>::nhoods;\
    using InitIndex<DataType>::SP;\
    using InitIndex<DataType>::nnExpansion;\
    using InitIndex<DataType>::nnExpansion_kgraph;
}
#endif