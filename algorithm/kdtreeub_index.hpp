#ifndef EFANNA_KDTREE_UB_INDEX_H_
#define EFANNA_KDTREE_UB_INDEX_H_
#include "algorithm/base_index.hpp"
#include <fstream>
#include <time.h>
#include <string.h>
#include <random>
//#include <bitset>
//using std::bitset;
#include <boost/dynamic_bitset.hpp>

namespace efanna{
  struct KDTreeUbIndexParams : public IndexParams
  {
      KDTreeUbIndexParams(bool rnn_used, int tree_num_total, int merge_level, int epoches = 4, int check = 25, int myL = 30, int building_use_k = 10, int tree_num_build = 0, int myS = 10)
      {
          reverse_nn_used = rnn_used;
          init_index_type = KDTREE_UB;
          K = building_use_k;
          build_epoches = epoches;
          S = myS;
          ValueType treev;
          treev.int_val = tree_num_total;
          extra_params.insert(std::make_pair("trees",treev));
          ValueType treeb;
          treeb.int_val = tree_num_build > 0 ? tree_num_build : tree_num_total;
          extra_params.insert(std::make_pair("treesb",treeb));
          ValueType merge_levelv;
          merge_levelv.int_val = merge_level;
          extra_params.insert(std::make_pair("ml",merge_levelv));
          L = myL;
          Check_K = check;
      }
  };
  template <typename DataType>
  class KDTreeUbIndex : public InitIndex<DataType>
  {
  public:

    typedef InitIndex<DataType> BaseClass;
    KDTreeUbIndex(const Matrix<DataType>& dataset, const Distance<DataType>* d, const IndexParams& params = KDTreeUbIndexParams(true,4)) :
      BaseClass(dataset,d,params)
    {
      std::cout<<"kdtree ub initial"<<std::endl;
      ExtraParamsMap::const_iterator it = params_.extra_params.find("trees");
      if(it != params_.extra_params.end()){
        TreeNum = (it->second).int_val;
        std::cout << "Using kdtree ub to build "<< TreeNum << " trees in total" << std::endl;
      }
      else{
        TreeNum = 4;
        std::cout << "Using kdtree ub to build "<< TreeNum << " trees in total" << std::endl;
      }
      SP.tree_num = TreeNum;

      it = params_.extra_params.find("treesb");
      if(it != params_.extra_params.end()){
        TreeNumBuild = (it->second).int_val;
        std::cout << "Building kdtree ub index with "<< TreeNumBuild <<" trees"<< std::endl;
      }
      else{
        TreeNumBuild = TreeNum;
        std::cout << "Building kdtree ub index with "<< TreeNumBuild <<" trees"<< std::endl;
      }

      it = params_.extra_params.find("ml");
      if(it != params_.extra_params.end()){
        ml = (it->second).int_val;
        std::cout << "Building kdtree ub initial index with merge level "<< ml  << std::endl;
      }
      else{
        ml = -1;
        std::cout << "Building kdtree ub initial index with max merge level "<< std::endl;
      }
      max_deepth = 0x0fffffff;
      error_flag = false;
    }

    void buildIndexImpl(){
      clock_t s,f;
      s = clock();
      initGraph();
      f = clock();
      std::cout << "initial graph using time: "<< (f-s)*1.0/CLOCKS_PER_SEC<<" seconds"<< std::endl;

      if(error_flag){
        std::cout << "merge level deeper than tree, max merge deepth is" << max_deepth-1<<std::endl;
        return;
      }
      refineGraph();
    }
    struct Node
    {
        int DivDim;
        DataType DivVal;
        size_t StartIdx, EndIdx;
        Node* Lchild, * Rchild;

        ~Node() {
          if (Lchild!=NULL) Lchild->~Node();
          if (Rchild!=NULL) Rchild->~Node();
        }

    };

    void loadIndex(char* filename){
	     read_data(filename);
    }
    void saveIndex(char* filename){

         size_t points_num = features_.get_rows();
         size_t feature_dim = features_.get_cols();
         save_data(filename, params_.K, points_num, feature_dim);
    }
    //algorithms copy and rewrite from flann
    void loadTrees(char* filename){
      std::ifstream in(filename, std::ios::binary|std::ios::in);
    	if(!in.is_open()){std::cout<<"open file error"<<std::endl;exit(-10087);}
    	unsigned int K,tree_num;
    	size_t dim,num;

    	//read file head
    	in.read((char*)&(K),sizeof(unsigned int));
    	in.read((char*)&(tree_num),sizeof(unsigned int));
    	in.read((char*)&(num),sizeof(size_t));
    	in.read((char*)&(dim),sizeof(size_t));

    	SP.tree_num = tree_num;

	     //read trees

      tree_roots_.clear();
    	for(unsigned int i=0;i<tree_num;i++){// for each tree
    		int node_num, node_size;
    		in.read((char*)&(node_num),sizeof(int));
    		in.read((char*)&(node_size),sizeof(int));

    		std::vector<struct Node *> tree_nodes;
    		for(int j=0;j<node_num;j++){
    			struct Node *tmp = new struct Node();
    			in.read((char*)&(tmp->DivDim),sizeof(tmp->DivDim));
    			in.read((char*)&(tmp->DivVal),sizeof(tmp->DivVal));
    			in.read((char*)&(tmp->StartIdx),sizeof(tmp->StartIdx));
    			in.read((char*)&(tmp->EndIdx),sizeof(tmp->EndIdx));
    			in.read((char*)&(tmp->Lchild),sizeof(tmp->Lchild));
    			in.read((char*)&(tmp->Rchild),sizeof(tmp->Rchild));
    			tmp->Lchild = NULL;
    			tmp->Rchild = NULL;
    			tree_nodes.push_back(tmp);


    		}
        //std::cout<<"build "<<i<<std::endl;
    		struct Node *root = DepthFirstBuildTree(tree_nodes);
    		if(root==NULL){ exit(-11); }
    		tree_roots_.push_back(root);
    	}

	     //read index range
      LeafLists.clear();
      for(unsigned int i=0;i<tree_num;i++){

      	std::vector<int> leaves;
      	for(unsigned int j=0;j<num; j++){
      		int leaf;
      		in.read((char*)&(leaf),sizeof(int));
      		leaves.push_back(leaf);
      	}
      	LeafLists.push_back(leaves);
      }
      in.close();
    }
    void saveTrees(char* filename){
        unsigned int K = params_.K;
        size_t num = features_.get_rows();
        size_t dim = features_.get_cols();
        std::fstream out(filename, std::ios::binary|std::ios::out);
      	if(!out.is_open()){std::cout<<"open file error"<<std::endl;exit(-10086);}
      	unsigned int tree_num = tree_roots_.size();

      	//write file head
      	out.write((char *)&(K), sizeof(unsigned int));
      	out.write((char *)&(tree_num), sizeof(unsigned int));
      	out.write((char *)&(num), sizeof(size_t)); //feature point number
      	out.write((char *)&(dim), sizeof(size_t)); //feature dim

      	//write trees
      	typename std::vector<Node *>::iterator it;//int cnt=0;
        for(it=tree_roots_.begin(); it!=tree_roots_.end(); it++){
          //write tree nodes with depth first trace


          size_t offset_node_num = out.tellp();

          out.seekp(sizeof(int),std::ios::cur);

          unsigned int node_size = sizeof(struct Node);
          out.write((char *)&(node_size), sizeof(int));

          unsigned int node_num = DepthFirstWrite(out, *it);

          out.seekg(offset_node_num,std::ios::beg);

          out.write((char *)&(node_num), sizeof(int));

          out.seekp(0,std::ios::end);
          //std::cout<<"tree: "<<cnt++<<" written, node: "<<node_num<<" at offset " << offset_node_num <<std::endl;
        }

      	if(LeafLists.size()!=tree_num){ std::cout << "leaf_size!=tree_num" << std::endl; exit(-6); }

      	for(unsigned int i=0; i<tree_num; i++){
      		for(unsigned int j=0;j<num;j++){
      			out.write((char *)&(LeafLists[i][j]), sizeof(int));
      		}
      	}
        out.close();
    }
    void loadGraph(char* filename){
      std::ifstream in(filename,std::ios::binary);
      in.seekg(0,std::ios::end);
      std::ios::pos_type ss = in.tellg();
      size_t fsize = (size_t)ss;
      int dim;
      in.seekg(0,std::ios::beg);
      in.read((char*)&dim, sizeof(int));
      size_t num = fsize / (dim+1) / 4;
      //std::cout<<"load g "<<num<<" "<<dim<< std::endl;
      in.seekg(0,std::ios::beg);
      knn_graph.clear();
      for(size_t i = 0; i < num; i++){
        CandidateHeap heap;
        in.read((char*)&dim, sizeof(int));
        for(int j =0; j < dim; j++){
          int id;
          in.read((char*)&id, sizeof(int));
          Candidate<DataType> can(id, -1);
          heap.insert(can);
        }
        knn_graph.push_back(heap);
      }
      in.close();
    }
    void saveGraph(char* filename){
     std::ofstream out(filename,std::ios::binary);

     int dim = params_.K;//int meansize = 0;
     for(size_t i = 0; i < knn_graph.size(); i++){
       typename CandidateHeap::reverse_iterator it = knn_graph[i].rbegin();
       out.write((char*)&dim, sizeof(int));//meansize += knn_graph[i].size();
       for(size_t j =0; j < params_.K && it!= knn_graph[i].rend(); j++,it++ ){
         int id = it->row_id;
         out.write((char*)&id, sizeof(int));
       }
     }//meansize /= knn_graph.size();
     //std::cout << "size mean " << meansize << std::endl;
     out.close();
    }
//for nn search
Node* SearchQueryInTree(Node* node, size_t id, const Matrix<DataType>& query, int depth){
      if(node->Lchild != NULL && node->Rchild !=NULL){
        if(depth == SP.search_depth)return node;
        depth++;
        if(query.get_row(id)[node->DivDim] < node->DivVal)
          return SearchQueryInTree(node->Lchild, id, query, depth);
        else
          return SearchQueryInTree(node->Rchild, id, query, depth);
      }
      else
        return node;

    }

  void getNeighbors(size_t k, const Matrix<DataType>& query){
      std::cout<<"using tree num "<< SP.tree_num<<std::endl;
      if(SP.tree_num > tree_roots_.size()){
        std::cout<<"wrong tree number"<<std::endl;return;
      }

      boost::dynamic_bitset<> tbflag(features_.get_rows(), false);
      boost::dynamic_bitset<> newflag(features_.get_rows(), false);

      nn_results.clear();

      for(unsigned int cur = 0; cur < query.get_rows(); cur++){

        CandidateHeap Candidates;
        tbflag.reset();
        newflag.reset();

       for(unsigned int i = 0; i < SP.tree_num; i++){
         if(SP.search_depth<0)break;
         Node* leafn = SearchQueryInTree(tree_roots_[i], cur, query, 0);
         for(size_t j = leafn->StartIdx; j < leafn->EndIdx; j++){
            size_t nn = LeafLists[i][j];

            if(tbflag.test(nn))continue;
            else{ tbflag.set(nn); newflag.set(nn);}

            Candidate<DataType> c(nn, distance_->compare(
              query.get_row(cur), features_.get_row(nn), features_.get_cols())
            );
            Candidates.insert(c);
            if(Candidates.size() > (unsigned int)SP.search_init_num)
              Candidates.erase(Candidates.begin());
         }
       }
       int base_n = features_.get_rows();

       while(Candidates.size() < (unsigned int)SP.search_init_num){
            unsigned int nn = rand() % base_n;

            if(tbflag.test(nn))continue;
            else{ tbflag.set(nn); newflag.set(nn);}

            Candidate<DataType> c(nn, distance_->compare(
                  query.get_row(cur), features_.get_row(nn), features_.get_cols())
                );
          Candidates.insert(c);
       }


       int iter=0;
         std::vector<int> ids;
         while(iter++ < SP.search_epoches){
           //the heap is max heap
           typename CandidateHeap::reverse_iterator it = Candidates.rbegin();
           ids.clear();
           for(int j = 0; j < SP.extend_to && it != Candidates.rend(); j++,it++){
             if(newflag.test(it->row_id)){
                newflag.reset(it->row_id);

                typename CandidateHeap::reverse_iterator neighbor = knn_graph[it->row_id].rbegin();

                for(size_t nnk = 0;nnk < params_.K && neighbor != knn_graph[it->row_id].rend(); neighbor++, nnk++){
                  if(tbflag.test(neighbor->row_id))continue;
                  else tbflag.set(neighbor->row_id);

                    ids.push_back(neighbor->row_id);
                }
               }
            }
           for(size_t j = 0; j < ids.size(); j++){

             Candidate<DataType> c(ids[j], distance_->compare(
                 query.get_row(cur), features_.get_row(ids[j]), features_.get_cols())
               );

             Candidates.insert(c);
             newflag.set(ids[j]);
             if(Candidates.size() > (unsigned int)SP.search_init_num)Candidates.erase(Candidates.begin());
           }
         }
         typename CandidateHeap::reverse_iterator it = Candidates.rbegin();
         std::vector<int> res;
         for(size_t i = 0; i < k && it != Candidates.rend();i++, it++)
            res.push_back(it->row_id);
         nn_results.push_back(res);

      }
  }

  void getNeighbors_version2(size_t K, const Matrix<DataType>& query){
        std::cout<<"using tree num "<< SP.tree_num<<std::endl;
        if(SP.tree_num > tree_roots_.size()){
          std::cout<<"wrong tree number"<<std::endl;return;
        }

        boost::dynamic_bitset<> tbflag(features_.get_rows(), false);

        nn_results.clear();

        for(unsigned int cur = 0; cur < query.get_rows(); cur++){

           	std::vector<unsigned int> pool;
            tbflag.reset();

           for(unsigned int i = 0; i < SP.tree_num; i++){
             if(SP.search_depth<0)break;
             Node* leafn = SearchQueryInTree(tree_roots_[i], cur, query, 0);
             for(size_t j = leafn->StartIdx; j < leafn->EndIdx; j++){
                size_t nn = LeafLists[i][j];

                if(tbflag.test(nn))continue;
                tbflag.set(nn);

                pool.push_back(nn);

             }
           }

           int base_n = features_.get_rows();

           while(pool.size() < (unsigned int)SP.search_init_num){
                unsigned int nn = rand() % base_n;

                if(tbflag.test(nn))continue;
                tbflag.set(nn);
                pool.push_back(nn);
           }
           SP.search_init_num = pool.size();

           std::vector<int> res;
           nnExpansion(K, query.get_row(cur), pool, res);
           nn_results.push_back(res);

        }
}

    int DepthFirstWrite(std::fstream& out, struct Node *root){
      if(root==NULL) return 0;
    	int left_cnt = DepthFirstWrite(out, root->Lchild);
    	int right_cnt = DepthFirstWrite(out, root->Rchild);

    	//std::cout << root->StartIdx <<":" << root->EndIdx<< std::endl;
    	out.write((char *)&(root->DivDim), sizeof(root->DivDim));
    	out.write((char *)&(root->DivVal), sizeof(root->DivVal));
    	out.write((char *)&(root->StartIdx), sizeof(root->StartIdx));
    	out.write((char *)&(root->EndIdx), sizeof(root->EndIdx));
    	out.write((char *)&(root->Lchild), sizeof(root->Lchild));
    	out.write((char *)&(root->Rchild), sizeof(root->Rchild));
    	return (left_cnt + right_cnt + 1);
    }
    struct Node* DepthFirstBuildTree(std::vector<struct Node *>& tree_nodes){
      std::vector<Node*> root_serial;
    	typename std::vector<struct Node*>::iterator it = tree_nodes.begin();
      for( ; it!=tree_nodes.end(); it++){
        Node* tmp = *it;
        size_t rsize = root_serial.size();
        if(rsize<2){
        	root_serial.push_back(tmp);
      	//continue;
        }
        else{
    			Node *last1 = root_serial[rsize-1];
    			Node *last2 = root_serial[rsize-2];
    			if(last1->EndIdx == tmp->EndIdx && last2->StartIdx == tmp->StartIdx){
    				tmp->Rchild = last1;
    				tmp->Lchild = last2;
    				root_serial.pop_back();
    				root_serial.pop_back();
    			}
    			root_serial.push_back(tmp);
        }

    	}
    	if(root_serial.size()!=1){
    		std::cout << "Error constructing trees" << std::endl;
    		return NULL;
    	}
    	return root_serial[0];
    }
    void read_data(char *filename){
      std::ifstream in(filename, std::ios::binary|std::ios::in);
    	if(!in.is_open()){std::cout<<"open file error"<<std::endl;exit(-10087);}
    	unsigned int K,tree_num;
    	size_t dim,num;

    	//read file head
    	in.read((char*)&(K),sizeof(unsigned int));
    	in.read((char*)&(tree_num),sizeof(unsigned int));
    	in.read((char*)&(num),sizeof(size_t));
    	in.read((char*)&(dim),sizeof(size_t));

    	SP.tree_num = tree_num;

    	//read trees

      tree_roots_.clear();
    	for(unsigned int i=0;i<tree_num;i++){// for each tree
    		int node_num, node_size;
    		in.read((char*)&(node_num),sizeof(int));
    		in.read((char*)&(node_size),sizeof(int));

    		std::vector<struct Node *> tree_nodes;
    		for(int j=0;j<node_num;j++){
    			struct Node *tmp = new struct Node();
    			in.read((char*)&(tmp->DivDim),sizeof(tmp->DivDim));
    			in.read((char*)&(tmp->DivVal),sizeof(tmp->DivVal));
    			in.read((char*)&(tmp->StartIdx),sizeof(tmp->StartIdx));
    			in.read((char*)&(tmp->EndIdx),sizeof(tmp->EndIdx));
    			in.read((char*)&(tmp->Lchild),sizeof(tmp->Lchild));
    			in.read((char*)&(tmp->Rchild),sizeof(tmp->Rchild));
    			tmp->Lchild = NULL;
    			tmp->Rchild = NULL;
    			tree_nodes.push_back(tmp);


    		}
        //std::cout<<"build "<<i<<std::endl;
    		struct Node *root = DepthFirstBuildTree(tree_nodes);
    		if(root==NULL){ exit(-11); }
    		tree_roots_.push_back(root);
    	}

    	//read index range
    	LeafLists.clear();
    	for(unsigned int i=0;i<tree_num;i++){

    		std::vector<int> leaves;
    		for(unsigned int j=0;j<num; j++){
    			int leaf;
    			in.read((char*)&(leaf),sizeof(int));
    			leaves.push_back(leaf);
    		}
    		LeafLists.push_back(leaves);
    	}

    	//read knn graph
    	knn_graph.clear();
    	for(size_t i = 0; i < num; i++){
    		CandidateHeap heap;
    		for(size_t j =0; j < K ; j++ ){
    			int id;
    			in.read((char*)&id, sizeof(int));
    			Candidate<DataType> can(id, -1);
    			heap.insert(can);
    		}
    		knn_graph.push_back(heap);
    	}
    	in.close();
    }
    void save_data(char *filename, unsigned int K, size_t num, size_t dim){
      std::fstream out(filename, std::ios::binary|std::ios::out);
    	if(!out.is_open()){std::cout<<"open file error"<<std::endl;exit(-10086);}
    	unsigned int tree_num = tree_roots_.size();

    	//write file head
    	out.write((char *)&(K), sizeof(unsigned int));
    	out.write((char *)&(tree_num), sizeof(unsigned int));
    	out.write((char *)&(num), sizeof(size_t)); //feature point number
    	out.write((char *)&(dim), sizeof(size_t)); //feature dim

    	//write trees
    	typename std::vector<Node *>::iterator it;//int cnt=0;
    	for(it=tree_roots_.begin(); it!=tree_roots_.end(); it++){
    		//write tree nodes with depth first trace


    		size_t offset_node_num = out.tellp();

    		out.seekp(sizeof(int),std::ios::cur);

    		unsigned int node_size = sizeof(struct Node);
    		out.write((char *)&(node_size), sizeof(int));

    		unsigned int node_num = DepthFirstWrite(out, *it);

    		out.seekg(offset_node_num,std::ios::beg);

    		out.write((char *)&(node_num), sizeof(int));

    		out.seekp(0,std::ios::end);
        //std::cout<<"tree: "<<cnt++<<" written, node: "<<node_num<<" at offset " << offset_node_num <<std::endl;
    	}

    	if(LeafLists.size()!=tree_num){ std::cout << "leaf_size!=tree_num" << std::endl; exit(-6); }

    	for(unsigned int i=0; i<tree_num; i++){
    		for(unsigned int j=0;j<num;j++){
    			out.write((char *)&(LeafLists[i][j]), sizeof(int));
    		}
    	}

    	//write knn-graph

    	if(knn_graph.size()!=num){std::cout << "Error:" << std::endl; exit(-1);}
    	for(size_t i = 0; i < knn_graph.size(); i++){
    		typename CandidateHeap::reverse_iterator it = knn_graph[i].rbegin();
    		for(size_t j =0; j < K && it!= knn_graph[i].rend(); j++,it++ ){
    			int id = it->row_id;
    			out.write((char*)&id, sizeof(int));
    		}
    	}

    	out.close();
    }
    Node* divideTree(std::mt19937& rng, int* indices, size_t count, size_t offset){
      Node* node = new Node();
      if(count <= params_.TNS){
        node->DivDim = -1;
        node->Lchild = NULL;
        node->Rchild = NULL;
        node->StartIdx = offset;
        node->EndIdx = offset + count;
        //add points

        for(size_t i = 0; i < count; i++){
          for(size_t j = i+1; j < count; j++){
            DataType dist = distance_->compare(
                features_.get_row(indices[i]), features_.get_row(indices[j]), features_.get_cols());

            if(knn_graph[indices[i]].size() < params_.S || dist < knn_graph[indices[i]].begin()->distance){
              Candidate<DataType> c1(indices[j], dist);
              knn_graph[indices[i]].insert(c1);
              if(knn_graph[indices[i]].size() > params_.S)knn_graph[indices[i]].erase(knn_graph[indices[i]].begin());
            }
            else if(nhoods[indices[i]].nn_new.size() < params_.S * 2)nhoods[indices[i]].nn_new.push_back(indices[j]);
            if(knn_graph[indices[j]].size() < params_.S || dist < knn_graph[indices[j]].begin()->distance){
              Candidate<DataType> c2(indices[i], dist);
              knn_graph[indices[j]].insert(c2);
              if(knn_graph[indices[j]].size() > params_.S)knn_graph[indices[j]].erase(knn_graph[indices[j]].begin());
            }
            else if(nhoods[indices[j]].nn_new.size() < params_.S * 2)nhoods[indices[j]].nn_new.push_back(indices[i]);
          }
        }

      }else{
        int idx;
        int cutdim;
        DataType cutval;
        meanSplit(rng, indices, count, idx, cutdim, cutval);

        node->DivDim = cutdim;
        node->DivVal = cutval;
        node->StartIdx = offset;
        node->EndIdx = offset + count;
        node->Lchild = divideTree(rng, indices, idx, offset);
        node->Rchild = divideTree(rng, indices+idx, count-idx, offset+idx);
      }

      return node;
    }

    Node* divideTreeOnly(std::mt19937& rng, int* indices, size_t count, size_t offset){
      Node* node = new Node();
      if(count <= params_.TNS){
        node->DivDim = -1;
        node->Lchild = NULL;
        node->Rchild = NULL;
        node->StartIdx = offset;
        node->EndIdx = offset + count;
        //add points

      }else{
        int idx;
        int cutdim;
        DataType cutval;
        meanSplit(rng, indices, count, idx, cutdim, cutval);

        node->DivDim = cutdim;
        node->DivVal = cutval;
        node->StartIdx = offset;
        node->EndIdx = offset + count;
        node->Lchild = divideTreeOnly(rng, indices, idx, offset);
        node->Rchild = divideTreeOnly(rng, indices+idx, count-idx, offset+idx);
      }

      return node;
    }


    void meanSplit(std::mt19937& rng, int* indices, int count, int& index, int& cutdim, DataType& cutval){
      size_t veclen_ = features_.get_cols();
      memset(mean_,0,veclen_*sizeof(DataType));
      memset(var_,0,veclen_*sizeof(DataType));

      /* Compute mean values.  Only the first SAMPLE_NUM values need to be
          sampled to get a good estimate.
       */
      int cnt = std::min((int)SAMPLE_NUM+1, count);
      for (int j = 0; j < cnt; ++j) {
          const DataType* v = features_.get_row(indices[j]);
          for (size_t k=0; k<veclen_; ++k) {
              mean_[k] += v[k];
          }
      }
      DataType div_factor = DataType(1)/cnt;
      for (size_t k=0; k<veclen_; ++k) {
          mean_[k] *= div_factor;
      }

      /* Compute variances (no need to divide by count). */

      for (int j = 0; j < cnt; ++j) {
          const DataType* v = features_.get_row(indices[j]);
          for (size_t k=0; k<veclen_; ++k) {
              DataType dist = v[k] - mean_[k];
              var_[k] += dist * dist;
          }
      }

      /* Select one of the highest variance indices at random. */
      cutdim = selectDivision(rng, var_);

      cutval = mean_[cutdim];

      int lim1, lim2;
      planeSplit(indices, count, cutdim, cutval, lim1, lim2);
      //cut the subtree using the id which best balances the tree
      if (lim1>count/2) index = lim1;
      else if (lim2<count/2) index = lim2;
      else index = count/2;

      /* If either list is empty, it means that all remaining features
       * are identical. Split in the middle to maintain a balanced tree.
       */
      if ((lim1==count)||(lim2==0)) index = count/2;
    }
    void planeSplit(int* indices, int count, int cutdim, DataType cutval, int& lim1, int& lim2){
      /* Move vector indices for left subtree to front of list. */
      int left = 0;
      int right = count-1;
      for (;; ) {
          while (left<=right && features_.get_row(indices[left])[cutdim]<cutval) ++left;
          while (left<=right && features_.get_row(indices[right])[cutdim]>=cutval) --right;
          if (left>right) break;
          std::swap(indices[left], indices[right]); ++left; --right;
      }
      lim1 = left;//lim1 is the id of the leftmost point <= cutval
      right = count-1;
      for (;; ) {
          while (left<=right && features_.get_row(indices[left])[cutdim]<=cutval) ++left;
          while (left<=right && features_.get_row(indices[right])[cutdim]>cutval) --right;
          if (left>right) break;
          std::swap(indices[left], indices[right]); ++left; --right;
      }
      lim2 = left;//lim2 is the id of the leftmost point >cutval
    }
    int selectDivision(std::mt19937& rng, DataType* v){
      int num = 0;
      size_t topind[RAND_DIM];

      //Create a list of the indices of the top RAND_DIM values.
      for (size_t i = 0; i < features_.get_cols(); ++i) {
          if ((num < RAND_DIM)||(v[i] > v[topind[num-1]])) {
              // Put this element at end of topind.
              if (num < RAND_DIM) {
                  topind[num++] = i;            // Add to list.
              }
              else {
                  topind[num-1] = i;         // Replace last element.
              }
              // Bubble end value down to right location by repeated swapping. sort the varience in decrease order
              int j = num - 1;
              while (j > 0  &&  v[topind[j]] > v[topind[j-1]]) {
                  std::swap(topind[j], topind[j-1]);
                  --j;
              }
          }
      }
      // Select a random integer in range [0,num-1], and return that index.
      int rnd = rng()%num;
      return (int)topind[rnd];
    }
    void getMergeLevelNodeList(Node* node, size_t treeid, int deepth){
      if(node->Lchild != NULL && node->Rchild != NULL && deepth < ml){
        deepth++;
        getMergeLevelNodeList(node->Lchild, treeid, deepth);
        getMergeLevelNodeList(node->Rchild, treeid, deepth);
      }else if(deepth == ml){
        mlNodeList.push_back(std::make_pair(node,treeid));
      }else{
        error_flag = true;
        if(deepth < max_deepth)max_deepth = deepth;
      }
    }
    Node* SearchToLeaf(Node* node, size_t id){
      if(node->Lchild != NULL && node->Rchild !=NULL){
        if(features_.get_row(id)[node->DivDim] < node->DivVal)
          return SearchToLeaf(node->Lchild, id);
        else
          return SearchToLeaf(node->Rchild, id);
      }
      else
        return node;
    }int cc = 0;
    void mergeSubGraphs(size_t treeid, Node* node){
      if(node->Lchild != NULL && node->Rchild != NULL){
        mergeSubGraphs(treeid, node->Lchild);
        mergeSubGraphs(treeid, node->Rchild);

        size_t numL = node->Lchild->EndIdx - node->Lchild->StartIdx;
        size_t numR = node->Rchild->EndIdx - node->Rchild->StartIdx;
        size_t start,end;
        Node * root;
        if(numL < numR){
          root = node->Rchild;
          start = node->Lchild->StartIdx;
          end = node->Lchild->EndIdx;
        }else{
          root = node->Lchild;
          start = node->Rchild->StartIdx;
          end = node->Rchild->EndIdx;
        }

        for(;start < end; start++){

          size_t feature_id = LeafLists[treeid][start];

          Node* leaf = SearchToLeaf(root, feature_id);
          for(size_t i = leaf->StartIdx; i < leaf->EndIdx; i++){
            size_t tmpfea = LeafLists[treeid][i];
            DataType dist = distance_->compare(
                features_.get_row(tmpfea), features_.get_row(feature_id), features_.get_cols());
            if(knn_graph[tmpfea].size() < params_.S || dist < knn_graph[tmpfea].begin()->distance){
              Candidate<DataType> c1(feature_id, dist);
              knn_graph[tmpfea].insert(c1);
              if(knn_graph[tmpfea].size() > params_.S)knn_graph[tmpfea].erase(knn_graph[tmpfea].begin());
            }
            else if(nhoods[tmpfea].nn_new.size() < params_.S * 2)nhoods[tmpfea].nn_new.push_back(feature_id);
            if(knn_graph[feature_id].size() < params_.S || dist < knn_graph[feature_id].begin()->distance){
              Candidate<DataType> c1(tmpfea, dist);
              knn_graph[feature_id].insert(c1);
              if(knn_graph[feature_id].size() > params_.S)knn_graph[feature_id].erase(knn_graph[feature_id].begin());
            }
            else if(nhoods[feature_id].nn_new.size() < params_.S * 2)nhoods[feature_id].nn_new.push_back(tmpfea);

          }
        }
      }
    }

    typedef std::set<Candidate<DataType>, std::greater<Candidate<DataType>> > CandidateHeap;


  protected:
    enum
    {
        /**
         * To improve efficiency, only SAMPLE_NUM random values are used to
         * compute the mean and variance at each level when building a tree.
         * A value of 100 seems to perform as well as using all values.
         */
        SAMPLE_NUM = 100,
        /**
         * Top random dimensions to consider
         *
         * When creating random trees, the dimension on which to subdivide is
         * selected at random from among the top RAND_DIM dimensions with the
         * highest variance.  A value of 5 works well.
         */
        RAND_DIM=5
    };

    int TreeNum;
    int TreeNumBuild;
    int ml;   //merge_level
    int max_deepth;
    DataType* var_;
    bool error_flag;
    DataType* mean_;
    std::vector<Node*> tree_roots_;
    std::vector< std::pair<Node*,size_t> > mlNodeList;
    std::vector< std::pair<Node*,size_t> > qlNodeList;
    std::vector<std::vector<int>> LeafLists;
    USING_BASECLASS_SYMBOLS

    //kgraph code

    static void GenRandom (std::mt19937& rng, unsigned *addr, unsigned size, unsigned N) {
        for (unsigned i = 0; i < size; ++i) {
            addr[i] = rng() % (N - size);
        }
        std::sort(addr, addr + size);
        for (unsigned i = 1; i < size; ++i) {
            if (addr[i] <= addr[i-1]) {
                addr[i] = addr[i-1] + 1;
            }
        }
        unsigned off = rng() % N;
        for (unsigned i = 0; i < size; ++i) {
            addr[i] = (addr[i] + off) % N;
        }
    }
    void buildTrees(){
      std::mt19937 rng(1998);
      int N = features_.get_rows();
      for(int i = 0; i < N; i++){
        Neighbor nhood;
        nhood.nn_new.resize(params_.S * 2);
        nhood.nn_new.clear();
        nhood.pool.resize(params_.L+1);
        nhood.radius = std::numeric_limits<float>::max();
        nhoods.push_back(nhood);
      }
      size_t points_num = features_.get_rows();
      std::vector<int> indices(points_num);
      for (size_t i = 0; i < points_num; ++i) {
          indices[i] = int(i);
          CandidateHeap Cands;
          knn_graph.push_back(Cands);
      }

      int veclen_ = features_.get_cols();
      mean_ = new DataType[veclen_];
      var_ = new DataType[veclen_];

      tree_roots_.resize(TreeNum);
      /* Construct the randomized trees. */

      for (int i = 0; i < TreeNum; i++) {
          /* Randomize the order of vectors to allow for unbiased sampling. */
          std::random_shuffle(indices.begin(), indices.end());
          tree_roots_[i] = divideTreeOnly(rng, &indices[0], points_num, 0);
          LeafLists.push_back(indices);
      }

    }
    void buildTreesAndMerge(std::mt19937& rng){
      size_t points_num = features_.get_rows();
      std::vector<int> indices(points_num);
      for (size_t i = 0; i < points_num; ++i) {
          indices[i] = int(i);
          CandidateHeap Cands;
          knn_graph.push_back(Cands);
      }

      int veclen_ = features_.get_cols();
      mean_ = new DataType[veclen_];
      var_ = new DataType[veclen_];

      tree_roots_.resize(TreeNum);
      /* Construct the randomized trees. */

      for (int i = 0; i < TreeNumBuild; i++) {
          /* Randomize the order of vectors to allow for unbiased sampling. */
          std::random_shuffle(indices.begin(), indices.end());
          tree_roots_[i] = divideTreeOnly(rng, &indices[0], points_num, 0);
          LeafLists.push_back(indices);
      }
      for (int i = TreeNumBuild; i < TreeNum; i++) {
          /* Randomize the order of vectors to allow for unbiased sampling. */
          std::random_shuffle(indices.begin(), indices.end());
          tree_roots_[i] = divideTree(rng, &indices[0], points_num, 0);
          LeafLists.push_back(indices);
      }
      std::cout << "merge subgraphs" << std::endl;
      delete[] mean_;
      delete[] var_;

      for(size_t i = 0; i < tree_roots_.size(); i++){
        getMergeLevelNodeList(tree_roots_[i], i ,0);
      }
      for(size_t i = 0; i < mlNodeList.size(); i++){
        //std::cout <<mlNodeList[i].second<< ":" <<mlNodeList[i].first->StartIdx<<":"<<mlNodeList[i].first->EndIdx<< std::endl;
        mergeSubGraphs(mlNodeList[i].second, mlNodeList[i].first);
      }
    }
    void initGraph(){
      //std::cout << "kdtree ub initialize" << std::endl;
      std::mt19937 rng(1998);
      int N = features_.get_rows();
      for(int i = 0; i < N; i++){
        Neighbor nhood;
        nhood.nn_new.resize(params_.S * 2);
        nhood.nn_new.clear();
        nhood.pool.resize(params_.L+1);
        nhood.radius = std::numeric_limits<float>::max();
        nhoods.push_back(nhood);
      }

      buildTreesAndMerge(rng);
      //unsigned cnttt = 0;
      //for (unsigned n = 0; n < N; ++n) cnttt += nhoods[n].nn_new.size();
      //std::cout << cnttt / N << std::endl;
      std::vector<unsigned> random(params_.L+1);

      for (unsigned n = 0; n < (unsigned)N; ++n) {
          Neighbor &nhood = nhoods[n];
          Points &pool = nhood.pool;
          GenRandom(rng, &random[0], random.size(), N);
          nhood.L = params_.S;
          nhood.Range = params_.S;
          if(nhood.nn_new.size()<params_.S*2)nhood.nn_new.resize(params_.S*2);
          typename CandidateHeap::reverse_iterator rit = knn_graph[n].rbegin();
          unsigned l = 0;
          for (; l < nhood.L && rit != knn_graph[n].rend(); rit++, ++l) {
              Point &nn = pool[l];
              nn.id = rit->row_id;
              nhood.nn_new[l] = nn.id;
              nn.dist = rit->distance;
              nn.flag = true;//if(n==7030)std::cout<<nn.id<<":"<<nn.dist<<std::endl;
          }
          unsigned c = l;
          while(c<params_.L){
            pool[c].dist = distance_->compare(features_.get_row(n), features_.get_row(random[c]),features_.get_cols());
            pool[c].id = random[c];
            c++;
          }
          if(l<nhood.L)sort(pool.begin(), pool.begin() + nhood.L);
          c=l;
          while(c < params_.S*2){
             nhood.nn_new[c] = random[c];c++;
          }
      }

    }

  };

}
#endif