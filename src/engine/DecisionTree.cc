#include "DecisionTree.h"

#include <cstdio>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include "Parameters.h"
#include "Engine.h"

#define TEXT "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789<>=!."
#define WHITESPACE " \t\r\n"
#define COMMENT "#"

DecisionTree::DecisionTree(Parameters *p, DecisionTree::Type t, bool cmpC, std::vector<std::string> *a, DecisionTree::Node *r)
{
  params = p;
  type = t;
  compressChain = cmpC;
  attrs = a;
  root = r;
  this->updateLeafIds();
}

DecisionTree::~DecisionTree()
{
  delete attrs;
  delete root;
}

void DecisionTree::updateLeafIds()
{
  leafmap.clear();
  root->populateLeafIds(leafmap);
}

void DecisionTree::setLeafWeight(int id, float w)
{
  //XXX: no error checking here
  leafmap[id]->setWeight(w);
}

std::string DecisionTree::getLeafPath(int id)
{
  //XXX: no error checking here
  return leafmap[id]->getPath();
}

unsigned int DecisionTree::getMaxNode(DecisionTree::Node *node)
{
  if (node->isRoot())
    return 0;

  DecisionTree::Option *opt = node->getParent();
  DecisionTree::Query *query = opt->getParent();
  bool addnode = false;
  switch (type)
  {
    case STONE:
      if (query->getLabel()=="NEW" && opt->getLabel()!="N")
        addnode = true;
      break;
  }
  
  DecisionTree::Node *topnode = query->getParent();
  return this->getMaxNode(topnode) + (addnode?1:0);
}

float DecisionTree::getWeight(DecisionTree::GraphCollection *graphs, Go::Move move, bool updatetree)
{
  if (!graphs->getBoard()->validMove(move) || !move.isNormal())
    return -1;

  std::list<DecisionTree::Node*> *nodes = this->getLeafNodes(graphs,move,updatetree);
  float w = -1;
  if (nodes != NULL)
  {
    w = DecisionTree::combineNodeWeights(nodes);
    delete nodes;
  }

  return w;
}

void DecisionTree::updateDescent(DecisionTree::GraphCollection *graphs, Go::Move move)
{
  std::list<DecisionTree::Node*> *nodes = this->getLeafNodes(graphs,move,true);
  if (nodes != NULL)
    delete nodes;
}

void DecisionTree::updateDescent(DecisionTree::GraphCollection *graphs)
{
  Go::Board *board = graphs->getBoard();
  Go::Color col = board->nextToMove();
  for (int p=0; p<board->getPositionMax(); p++)
  {
    Go::Move move = Go::Move(col,p);
    if (board->validMove(move))
      this->updateDescent(graphs,move);
  }
}

void DecisionTree::collectionUpdateDescent(std::list<DecisionTree*> *trees, DecisionTree::GraphCollection *graphs)
{
  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    (*iter)->updateDescent(graphs);
  }
}

float DecisionTree::getCollectionWeight(std::list<DecisionTree*> *trees, DecisionTree::GraphCollection *graphs, Go::Move move, bool updatetree)
{
  float weight = 1;

  if (!graphs->getBoard()->validMove(move) || !move.isNormal())
    return -1;

  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    float w = (*iter)->getWeight(graphs, move, updatetree);
    if (w == -1)
      return -1;
    weight *= w;
  }

  return weight;
}

std::list<int> *DecisionTree::getLeafIds(DecisionTree::GraphCollection *graphs, Go::Move move)
{
  if (!graphs->getBoard()->validMove(move))
    return NULL;

  std::list<DecisionTree::Node*> *nodes = this->getLeafNodes(graphs,move,false);
  if (nodes == NULL)
    return NULL;

  std::list<int> *ids = new std::list<int>();
  for (std::list<DecisionTree::Node*>::iterator iter=nodes->begin();iter!=nodes->end();++iter)
  {
    ids->push_back((*iter)->getLeafId());
  }
  delete nodes;
  return ids;
}

std::list<int> *DecisionTree::getCollectionLeafIds(std::list<DecisionTree*> *trees, DecisionTree::GraphCollection *graphs, Go::Move move)
{
  std::list<int> *collids = new std::list<int>();
  int offset = 0;

  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    std::list<int> *ids = (*iter)->getLeafIds(graphs, move);
    if (ids == NULL)
    {
      delete collids;
      return NULL;
    }
    for (std::list<int>::iterator iter2=ids->begin();iter2!=ids->end();++iter2)
    {
      collids->push_back(offset + (*iter2));
    }
    delete ids;
    offset += (*iter)->getLeafCount();
  }

  return collids;
}

int DecisionTree::getCollectionLeafCount(std::list<DecisionTree*> *trees)
{
  int count = 0;

  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    count += (*iter)->getLeafCount();
  }

  return count;
}

void DecisionTree::setCollectionLeafWeight(std::list<DecisionTree*> *trees, int id, float w)
{
  int offset = 0;

  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    int lc = (*iter)->getLeafCount();
    if (offset <= id && id < (offset+lc))
    {
      (*iter)->setLeafWeight(id-offset,w);
      break;
    }
    offset += lc;
  }
}

std::string DecisionTree::getCollectionLeafPath(std::list<DecisionTree*> *trees, int id)
{
  int offset = 0;

  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    int lc = (*iter)->getLeafCount();
    if (offset <= id && id < (offset+lc))
      return (*iter)->getLeafPath(id-offset);
    offset += lc;
  }

  return "";
}

float DecisionTree::combineNodeWeights(std::list<DecisionTree::Node*> *nodes)
{
  float weight = 1;
  for (std::list<DecisionTree::Node*>::iterator iter=nodes->begin();iter!=nodes->end();++iter)
  {
    weight *= (*iter)->getWeight();
  }
  return weight;
}

void DecisionTree::getTreeStats(int &treenodes, int &leaves, int &maxdepth, float &avgdepth, int &maxnodes, float &avgnodes)
{
  treenodes = 0;
  leaves = 0;
  maxdepth = 0;
  int sumdepth = 0;
  maxnodes = 0;
  int sumnodes = 0;

  root->getTreeStats(type,1,1,treenodes,leaves,maxdepth,sumdepth,maxnodes,sumnodes);
  avgdepth = (float)sumdepth/leaves;
  avgnodes = (float)sumnodes/leaves;
}

void DecisionTree::Node::getTreeStats(DecisionTree::Type type, int depth, int nodes, int &treenodes, int &leaves, int &maxdepth, int &sumdepth, int &maxnodes, int &sumnodes)
{
  treenodes++;

  if (this->isLeaf())
  {
    leaves++;

    if (depth > maxdepth)
      maxdepth = depth;
    sumdepth += depth;

    if (nodes > maxnodes)
      maxnodes = nodes;
    sumnodes += nodes;
  }
  else if (query!=NULL)
  {
    std::vector<DecisionTree::Option*> *options = query->getOptions();
    for (unsigned int i=0; i<options->size(); i++)
    {
      DecisionTree::Option *opt = options->at(i);
      bool addnode = false;
      switch (type)
      {
        case STONE:
          if (query->getLabel()=="NEW" && opt->getLabel()!="N")
            addnode = true;
          break;
      }
      opt->getNode()->getTreeStats(type,depth+1,nodes+(addnode?1:0),treenodes,leaves,maxdepth,sumdepth,maxnodes,sumnodes);
    }
  }
}

int DecisionTree::getDistance(Go::Board *board, int p1, int p2)
{
  if (p1<0 && p2<0) // both sides
  {
    if ((p1+p2)%2==0) // opposite sides
      return board->getSize();
    else
      return 0;
  }
  else if (p1<0 || p2<0) // one side
  {
    if (p2 < 0)
    {
      int tmp = p1;
      p1 = p2;
      p2 = tmp;
    }
    // p1 is side

    int size = board->getSize();
    int x = Go::Position::pos2x(p2,size);
    int y = Go::Position::pos2y(p2,size);
    switch (p1)
    {
      case -1:
        return x;
      case -2:
        return y;
      case -3:
        return size-x-1;
      case -4:
        return size-y-1;
    }
    return 0;
  }
  else
    return board->getRectDistance(p1,p2);
}

bool DecisionTree::updateStoneNode(DecisionTree::Node *node, DecisionTree::StoneGraph *graph, std::vector<unsigned int> *stones, bool invert)
{
  std::vector<DecisionTree::StatPerm*> *statperms = node->getStats()->getStatPerms();
  if (node->isLeaf()) // only update stats until the node is split
  {
    for (unsigned int i=0; i<statperms->size(); i++)
    {
      DecisionTree::StatPerm *sp = statperms->at(i);

      std::string spt = sp->getLabel();
      std::vector<std::string> *attrs = sp->getAttrs();
      int res = 0;
      int resmin = sp->getRange()->getStart();
      int resmax = sp->getRange()->getEnd();
      //fprintf(stderr,"[DT] SP type: '%s'\n",spt.c_str());
      if (spt=="NEW")
      {
        unsigned int auxnode = stones->at(0);

        std::string cols = attrs->at(0);
        bool B = (cols.find('B') != std::string::npos);
        bool W = (cols.find('W') != std::string::npos);
        bool S = (cols.find('S') != std::string::npos);
        if (!B && !W && !S)
        {
          fprintf(stderr,"[DT] Error! Unknown attribute: '%s'\n",cols.c_str());
          return false;
        }

        bool resfound = false;
        for (int s=0; s<=resmax; s++)
        {
          for (unsigned int i=0; i<graph->getNumNodes(); i++)
          {
            bool valid = false;
            if ((invert?W:B) && graph->getNodeStatus(i)==Go::BLACK)
              valid = true;
            else if ((invert?B:W) && graph->getNodeStatus(i)==Go::WHITE)
              valid = true;
            else if (S && graph->getNodeStatus(i)==Go::OFFBOARD)
              valid = true;

            if (valid)
            {
              if (graph->getEdgeWeight(auxnode,i) <= s)
              {
                bool found = false;
                for (unsigned int j=0; j<stones->size(); j++)
                {
                  if (stones->at(j) == i)
                  {
                    found = true;
                    break;
                  }
                }
                if (!found)
                {
                  resfound = true;
                  res = s;
                  break;
                }
              }
            }
          }

          if (resfound)
            break;
        }

        if (!resfound)
          res = resmax;
      }
      else if (spt=="DIST")
      {
        std::vector<std::string> *attrs = sp->getAttrs();
        int n0 = boost::lexical_cast<int>(attrs->at(0));
        int n1 = boost::lexical_cast<int>(attrs->at(1));

        res = graph->getEdgeWeight(stones->at(n0),stones->at(n1));
      }
      else if (spt=="ATTR")
      {
        std::vector<std::string> *attrs = sp->getAttrs();
        std::string type = attrs->at(0);
        int n = boost::lexical_cast<int>(attrs->at(1));

        int node = stones->at(n);
        if (type == "SIZE")
          res = graph->getNodeSize(node);
        else if (type == "LIB")
          res = graph->getNodeLiberties(node);
      }
      else
      {
        fprintf(stderr,"[DT] Error! Unknown stat perm type: '%s'\n",spt.c_str());
        return false;
      }

      if (res < resmin)
        res = resmin;
      else if (res > resmax)
        res = resmax;
      sp->getRange()->addVal(res,params->dt_range_divide);
    }
  }

  if (node->isLeaf())
  {
    int descents = statperms->at(0)->getRange()->getThisVal();
    if (descents >= params->dt_split_after && (descents%10)==0)
    {
      //fprintf(stderr,"DT split now!\n");

      std::string bestlabel = "";
      std::vector<std::string> *bestattrs = NULL;
      float bestval = -1;

      for (unsigned int i=0; i<statperms->size(); i++)
      {
        DecisionTree::StatPerm *sp = statperms->at(i);
        DecisionTree::Range *range = sp->getRange();

        float m = range->getExpectedMedian();
        int m1 = floor(m);
        int m2 = ceil(m);
        float p1l = range->getExpectedPercentageLessThan(m1);
        float p1e = range->getExpectedPercentageEquals(m1);
        float p2l = (m1==m2?p1l:range->getExpectedPercentageLessThan(m2));
        float p2e = (m1==m2?p1e:range->getExpectedPercentageEquals(m2));

        float v1l = DecisionTree::percentageToVal(p1l);
        float v2l = DecisionTree::percentageToVal(p2l);
        float v1e = DecisionTree::percentageToVal(p1e);
        float v2e = DecisionTree::percentageToVal(p2e);

        /*fprintf(stderr,"DT stat: %s",sp->getLabel().c_str());
        for (unsigned int j=0; j<sp->getAttrs()->size(); j++)
          fprintf(stderr,"%c%s",(j==0?'[':'|'),sp->getAttrs()->at(j).c_str());
        fprintf(stderr,"] \t %.3f %d %d %.3f %.3f %.3f %.3f\n",m,m1,m2,p1l,p1e,p2l,p2e);*/
        //fprintf(stderr,"DT range:\n%s\n",range->toString().c_str());

        bool foundbest = false;
        int localm = 0;
        bool lne = false;

        if (sp->getLabel()=="NEW")
        {
          if (v1l > bestval)
          {
            bestval = v1l;
            localm = m1-1;
            foundbest = true;
          }
          if (v2l > bestval)
          {
            bestval = v2l;
            localm = m2-1;
            foundbest = true;
          }
        }
        else
        {
          if (v1l > bestval)
          {
            bestval = v1l;
            localm = m1;
            lne = true;
            foundbest = true;
          }
          if (v2l > bestval)
          {
            bestval = v2l;
            localm = m2;
            lne = true;
            foundbest = true;
          }
          if (v1e > bestval)
          {
            bestval = v1e;
            localm = m1;
            lne = false;
            foundbest = true;
          }
          if (v2e > bestval)
          {
            bestval = v2e;
            localm = m2;
            lne = false;
            foundbest = true;
          }
        }

        if (foundbest)
        {
          bestlabel = sp->getLabel();
          if (bestattrs != NULL)
            delete bestattrs;
          std::vector<std::string> *attrs = new std::vector<std::string>();
          for (unsigned int j=0; j<sp->getAttrs()->size(); j++)
          {
            attrs->push_back(sp->getAttrs()->at(j));
          }
          if (bestlabel=="DIST" || bestlabel=="ATTR")
            attrs->push_back(lne?"<":"=");
          attrs->push_back(boost::lexical_cast<std::string>(localm));
          bestattrs = attrs;
        }
      }

      if (bestval != -1)
      {
        if (bestval >= params->dt_split_threshold)
        {
          int maxnode = this->getMaxNode(node);

          DecisionTree::Query *query = new DecisionTree::Query(type,bestlabel,bestattrs,maxnode);
          query->setParent(node);
          node->setQuery(query);

          /*params->engine->getGtpEngine()->getOutput()->printfDebug("DT best stat: %s",bestlabel.c_str());
          for (unsigned int j=0; j<bestattrs->size(); j++)
            params->engine->getGtpEngine()->getOutput()->printfDebug("%c%s",(j==0?'[':'|'),bestattrs->at(j).c_str());
          params->engine->getGtpEngine()->getOutput()->printfDebug("] %.2f\n",bestval);*/
        }
        else
          delete bestattrs;
      }
    }
  }

  return true;
}

float DecisionTree::percentageToVal(float p)
{
  float val = (p-0.5)/0.5;
  if (val < 0)
    val = -val;
  val = 1 - val;
  return val;
}

std::list<DecisionTree::Node*> *DecisionTree::getLeafNodes(DecisionTree::GraphCollection *graphs, Go::Move move, bool updatetree)
{
  std::list<DecisionTree::Node*> *nodes = NULL;

  std::vector<unsigned int> *stones = new std::vector<unsigned int>();
  bool invert = (move.getColor() != Go::BLACK);

  switch (type)
  {
    case STONE:
      DecisionTree::StoneGraph *graph = graphs->getStoneGraph(this->getCompressChain());
      unsigned int auxnode = graph->addAuxNode(move.getPosition());

      stones->push_back(auxnode);
      nodes = this->getStoneLeafNodes(root,graph,stones,invert,updatetree);

      graph->removeAuxNode();
  }

  delete stones;

  if (nodes!=NULL && params->dt_solo_leaf)
  {
    if (nodes->size()>1)
    {
      int minid = -1;
      for (std::list<DecisionTree::Node*>::iterator iter=nodes->begin();iter!=nodes->end();++iter)
      {
        if (minid==-1 || (*iter)->getLeafId()<minid)
          minid = (*iter)->getLeafId();
      }
      std::list<DecisionTree::Node*> *newnodes = new std::list<DecisionTree::Node*>();;
      for (std::list<DecisionTree::Node*>::iterator iter=nodes->begin();iter!=nodes->end();++iter)
      {
        if ((*iter)->getLeafId()==minid)
        {
          newnodes->push_back((*iter));
          break;
        }
      }
      delete nodes;
      nodes = newnodes;
    }
  }

  return nodes;
}

std::list<DecisionTree::Node*> *DecisionTree::getStoneLeafNodes(DecisionTree::Node *node, DecisionTree::StoneGraph *graph, std::vector<unsigned int> *stones, bool invert, bool updatetree)
{
  if (updatetree)
  {
    if (!this->updateStoneNode(node,graph,stones,invert))
      return NULL;
  }

  if (node->isLeaf())
  {
    std::list<DecisionTree::Node*> *list = new std::list<DecisionTree::Node*>();
    list->push_back(node);
    return list;
  }

  DecisionTree::Query *q = node->getQuery();
  
  if (q->getLabel() == "NEW") // TODO: replace with StoneGraph::getSortedNodesFromAux()
  {
    unsigned int auxnode = stones->at(0);

    std::vector<std::string> *attrs = q->getAttrs();
    bool B = (attrs->at(0).find('B') != std::string::npos);
    bool W = (attrs->at(0).find('W') != std::string::npos);
    bool S = (attrs->at(0).find('S') != std::string::npos);
    std::string valstr = attrs->at(1);
    int val = boost::lexical_cast<int>(valstr);

    std::list<unsigned int> matches;
    for (int s=0; s<=val; s++)
    {
      for (unsigned int i=0; i<graph->getNumNodes(); i++)
      {
        bool valid = false;
        if ((invert?W:B) && graph->getNodeStatus(i)==Go::BLACK)
          valid = true;
        else if ((invert?B:W) && graph->getNodeStatus(i)==Go::WHITE)
          valid = true;
        else if (S && graph->getNodeStatus(i)==Go::OFFBOARD)
          valid = true;

        if (valid)
        {
          if (graph->getEdgeWeight(auxnode,i) <= s)
          {
            bool found = false;
            for (unsigned int j=0; j<stones->size(); j++)
            {
              if (stones->at(j) == i)
              {
                found = true;
                break;
              }
            }
            if (!found)
              matches.push_back(i);
          }
        }
      }

      if (matches.size() > 0)
        break;
    }

    if (matches.size() > 1) // try break ties (B...W...S)
    {
      std::list<unsigned int> m;
      for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
      {
        unsigned int n = (*iter);
        if (invert)
        {
          if (graph->getNodeStatus(n)==Go::WHITE)
            m.push_back(n);
        }
        else
        {
          if (graph->getNodeStatus(n)==Go::BLACK)
            m.push_back(n);
        }
      }
      if (m.size()==0)
      {
        for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
        {
          unsigned int n = (*iter);
          if (invert)
          {
            if (graph->getNodeStatus(n)==Go::BLACK)
              m.push_back(n);
          }
          else
          {
            if (graph->getNodeStatus(n)==Go::WHITE)
              m.push_back(n);
          }
        }
        if (m.size()==0)
        {
          for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
          {
            unsigned int n = (*iter);
            if (graph->getNodeStatus(n)==Go::OFFBOARD)
              m.push_back(n);
          }
        }
      }
      matches = m;

      if (matches.size() > 1) // still tied (dist to newest node)
      {
        for (unsigned int i=stones->size()-1; i>0; i--) // implicitly checked distance to 0'th node
        {
          int mindist = graph->getEdgeWeight(stones->at(i),matches.front());
          for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
          {
            int dist = graph->getEdgeWeight(stones->at(i),(*iter));
            if (dist < mindist)
              mindist = dist;
          }

          std::list<unsigned int> m;
          for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
          {
            int dist = graph->getEdgeWeight(stones->at(i),(*iter));
            if (dist == mindist)
              m.push_back((*iter));
          }
          matches = m;

          if (matches.size() == 1)
            break;
        }

        if (matches.size() > 1) // yet still tied (largest size)
        {
          int maxsize = 0;
          for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
          {
            int size = graph->getNodeSize((*iter));
            if (size > maxsize)
              maxsize = size;
          }

          std::list<unsigned int> m;
          for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
          {
            int size = graph->getNodeSize((*iter));
            if (size == maxsize)
              m.push_back((*iter));
          }
          matches = m;

          if (matches.size() > 1) // yet yet still tied (most liberties)
          {
            int maxlib = 0;
            for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
            {
              int lib = graph->getNodeLiberties((*iter));
              if (lib > maxlib)
                maxlib = lib;
            }

            std::list<unsigned int> m;
            for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
            {
              int lib = graph->getNodeLiberties((*iter));
              if (lib == maxlib)
                m.push_back((*iter));
            }
            matches = m;
          }
        }
      }
    }

    //fprintf(stderr,"[DT] matches: %lu\n",matches.size());
    if (matches.size() > 0)
    {
      std::list<DecisionTree::Node*> *nodes = NULL;
      for (std::list<unsigned int>::iterator iter=matches.begin();iter!=matches.end();++iter)
      {
        std::list<DecisionTree::Node*> *subnodes = NULL;
        unsigned int newnode = (*iter);
        Go::Color col = graph->getNodeStatus(newnode);
        stones->push_back(newnode);

        std::vector<DecisionTree::Option*> *options = q->getOptions();
        for (unsigned int i=0; i<options->size(); i++)
        {
          std::string l = options->at(i)->getLabel();
          if (col==Go::BLACK && l==(invert?"W":"B"))
            subnodes = this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
          else if (col==Go::WHITE && l==(invert?"B":"W"))
            subnodes = this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
          else if (col==Go::OFFBOARD && l=="S")
            subnodes = this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
        }
        stones->pop_back();

        if (subnodes == NULL)
        {
          if (nodes != NULL)
            delete nodes;
          return NULL;
        }

        if (nodes == NULL)
          nodes = subnodes;
        else
        {
          nodes->splice(nodes->begin(),*subnodes);
          delete subnodes;
        }
      }

      return nodes;
    }
    else
    {
      std::vector<DecisionTree::Option*> *options = q->getOptions();
      for (unsigned int i=0; i<options->size(); i++)
      {
        std::string l = options->at(i)->getLabel();
        if (l=="N")
          return this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
      }
      return NULL;
    }
  }
  else if (q->getLabel() == "DIST")
  {
    std::vector<std::string> *attrs = q->getAttrs();
    int n0 = boost::lexical_cast<int>(attrs->at(0));
    int n1 = boost::lexical_cast<int>(attrs->at(1));
    bool eq = attrs->at(2) == "=";
    int val = boost::lexical_cast<int>(attrs->at(3));

    int dist = graph->getEdgeWeight(stones->at(n0),stones->at(n1));
    bool res;
    if (eq)
      res = dist == val;
    else
      res = dist < val;

    std::vector<DecisionTree::Option*> *options = q->getOptions();
    for (unsigned int i=0; i<options->size(); i++)
    {
      std::string l = options->at(i)->getLabel();
      if (res && l=="Y")
        return this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
      else if (!res && l=="N")
        return this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
    }
    return NULL;
  }
  else if (q->getLabel() == "ATTR")
  {
    std::vector<std::string> *attrs = q->getAttrs();
    std::string type = attrs->at(0);
    int n = boost::lexical_cast<int>(attrs->at(1));
    bool eq = attrs->at(2) == "=";
    int val = boost::lexical_cast<int>(attrs->at(3));

    unsigned int node = stones->at(n);
    int attr = 0;
    if (type == "SIZE")
      attr = graph->getNodeSize(node);
    else if (type == "LIB")
      attr = graph->getNodeLiberties(node);

    bool res;
    if (eq)
      res = attr == val;
    else
      res = attr < val;

    std::vector<DecisionTree::Option*> *options = q->getOptions();
    for (unsigned int i=0; i<options->size(); i++)
    {
      std::string l = options->at(i)->getLabel();
      if (res && l=="Y")
        return this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
      else if (!res && l=="N")
        return this->getStoneLeafNodes(options->at(i)->getNode(),graph,stones,invert,updatetree);
    }
    return NULL;
  }
  else
  {
    fprintf(stderr,"[DT] Error! Invalid query type: '%s'\n",q->getLabel().c_str());
    return NULL;
  }
}

std::string DecisionTree::toString(bool ignorestats, int leafoffset)
{
  std::string r = "(DT[";
  for (unsigned int i=0;i<attrs->size();i++)
  {
    if (i!=0)
      r += "|";
    r += attrs->at(i);
  }
  r += "]\n";
  //r += " # leaves: " + boost::lexical_cast<std::string>(leafmap.size()) + "\n";

  r += root->toString(2,ignorestats,leafoffset);

  r += ")";
  return r;
}

std::string DecisionTree::Node::toString(int indent, bool ignorestats, int leafoffset)
{
  std::string r = "";

  if (ignorestats)
  {
    for (int i=0;i<indent;i++)
      r += " ";
    r += "(STATS:)\n";
  }
  else
    r += stats->toString(indent);

  if (query == NULL)
  {
    for (int i=0;i<indent;i++)
      r += " ";
    r += "(WEIGHT[";
    r += boost::lexical_cast<std::string>(weight); //TODO: make sure this is correctly formatted
    r += "]) # id: " + boost::lexical_cast<std::string>(leafoffset+leafid) + "\n";
  }
  else
    r += query->toString(indent,ignorestats,leafoffset);

  return r;
}

std::string DecisionTree::Node::getPath()
{
  std::string r = "";

  if (!this->isRoot())
  {
    DecisionTree::Option *opt = this->getParent();
    DecisionTree::Query *query = opt->getParent();
    DecisionTree::Node *node = query->getParent();

    r = node->getPath();
    r += query->getLabel() + "[";
    for (unsigned int i=0;i<query->getAttrs()->size();i++)
    {
      if (i!=0)
        r += "|";
      r += query->getAttrs()->at(i);
    }
    r += "]->";
    r += opt->getLabel() + ":";
  }

  if (query == NULL)
  {
    r += "WEIGHT[";
    r += boost::lexical_cast<std::string>(weight); //TODO: make sure this is correctly formatted
    r += "]";
  }

  return r;
}

std::string DecisionTree::Query::toString(int indent, bool ignorestats, int leafoffset)
{
  std::string r = "";

  for (int i=0;i<indent;i++)
    r += " ";
  r += "(" + label + "[";
  for (unsigned int i=0;i<attrs->size();i++)
  {
    if (i!=0)
      r += "|";
    r += attrs->at(i);
  }
  r += "]\n";

  for (unsigned int i=0;i<options->size();i++)
  {
    r += options->at(i)->toString(indent+2,ignorestats,leafoffset);
  }
  
  for (int i=0;i<indent;i++)
    r += " ";
  r += ")\n";

  return r;
}

std::string DecisionTree::Option::toString(int indent, bool ignorestats, int leafoffset)
{
  std::string r = "";

  for (int i=0;i<indent;i++)
    r += " ";
  r += label + ":\n";

  r += node->toString(indent+2,ignorestats,leafoffset);

  return r;
}

std::string DecisionTree::Stats::toString(int indent)
{
  std::string r = "";

  for (int i=0;i<indent;i++)
    r += " ";
  r += "(STATS:\n";

  for (unsigned int i=0;i<statperms->size();i++)
  {
    r += statperms->at(i)->toString(indent+2);
  }

  for (int i=0;i<indent;i++)
    r += " ";
  r += ")\n";

  return r;
}

std::string DecisionTree::StatPerm::toString(int indent)
{
  std::string r = "";

  for (int i=0;i<indent;i++)
    r += " ";
  r += "(" + label + "[";
  for (unsigned int i=0;i<attrs->size();i++)
  {
    if (i!=0)
      r += "|";
    r += attrs->at(i);
  }
  r += "]\n";

  r += range->toString(indent+2);

  for (int i=0;i<indent;i++)
    r += " ";
  r += ")\n";

  return r;
}

std::string DecisionTree::Range::toString(int indent)
{
  std::string r = "";

  for (int i=0;i<indent;i++)
    r += " ";
  r += "(";
  r += boost::lexical_cast<std::string>(start);
  r += ":";
  r += boost::lexical_cast<std::string>(end);
  r += "[";
  r += boost::lexical_cast<std::string>(val);
  r += "]";

  if (left==NULL && right==NULL)
    r += ")\n";
  else
  {
    r += "\n";

    if (left != NULL)
      r += left->toString(indent+2);
    if (right != NULL)
      r += right->toString(indent+2);

    for (int i=0;i<indent;i++)
      r += " ";
    r += ")\n";
  }

  return r;
}

std::list<DecisionTree*> *DecisionTree::parseString(Parameters *params, std::string rawdata, unsigned long pos)
{
  std::string data = DecisionTree::stripWhitespace(DecisionTree::stripComments(rawdata));
  std::transform(data.begin(),data.end(),data.begin(),::toupper);
  //fprintf(stderr,"[DT] parsing: '%s'\n",data.c_str());

  pos = data.find("(DT",pos);
  if (pos == std::string::npos)
  {
    fprintf(stderr,"[DT] Error! Missing '(DT'\n");
    return NULL;
  }
  pos += 3;

  if (data[pos] != '[')
  {
    fprintf(stderr,"[DT] Error! Expected '[' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos++;

  std::vector<std::string> *attrs = DecisionTree::parseAttrs(data,pos);
  if (attrs == NULL)
    return NULL;

  Type type;
  bool compressChain = false;

  std::string typestr = attrs->at(0); // tree type must be first attribute
  if (typestr == "STONE" || typestr=="SPARSE") // SPARSE is legacy
    type = STONE;
  else
  {
    fprintf(stderr,"[DT] Error! Invalid type: '%s'\n",typestr.c_str());
    delete attrs;
    return NULL;
  }

  for (unsigned int i=1; i<attrs->size(); i++)
  {
    if (attrs->at(i) == "CHAINCOMP")
      compressChain = true;
  }

  DecisionTree::Node *root = DecisionTree::parseNode(type,data,pos);
  if (root == NULL)
  {
    delete attrs;
    return NULL;
  }

  if (data[pos] != ')')
  {
    fprintf(stderr,"[DT] Error! Expected ')' at '%s'\n",data.substr(pos).c_str());
    delete root;
    delete attrs;
    return NULL;
  }
  pos++;

  root->populateEmptyStats(type);

  DecisionTree *dt = new DecisionTree(params,type,compressChain,attrs,root);
  
  bool isnext = false;
  if (pos < data.size())
  {
    pos = data.find("(DT",pos);
    if (pos != std::string::npos && pos < data.size())
      isnext = true;
  }
  if (isnext)
  {
    std::list<DecisionTree*> *trees = DecisionTree::parseString(params,data,pos);
    if (trees == NULL)
    {
      delete dt;
      return NULL;
    }
    trees->push_front(dt);
    return trees;
  }
  else
  {
    std::list<DecisionTree*> *trees = new std::list<DecisionTree*>();
    trees->push_back(dt);
    return trees;
  }
}

std::vector<std::string> *DecisionTree::parseAttrs(std::string data, unsigned long &pos)
{
  if (data[pos] == ']')
  {
    pos++;
    return new std::vector<std::string>();
  }

  std::string attr = "";
  while (pos<data.length() && DecisionTree::isText(data[pos]))
  {
    attr += data[pos];
    pos++;
  }
  if (attr.length() == 0)
  {
    fprintf(stderr,"[DT] Error! Unexpected '%c' at '%s'\n",data[pos],data.substr(pos).c_str());
    return NULL;
  }

  if (data[pos] == '|' || data[pos] == ']')
  {
    if (data[pos] == '|')
      pos++;
    std::vector<std::string> *attrstail = DecisionTree::parseAttrs(data,pos);
    if (attrstail == NULL)
      return NULL;
    std::vector<std::string> *attrs = new std::vector<std::string>();
    attrs->push_back(attr);
    for (unsigned int i=0;i<attrstail->size();i++)
    {
      attrs->push_back(attrstail->at(i));
    }
    delete attrstail;
    return attrs;
  }
  else
  {
    fprintf(stderr,"[DT] Error! Unexpected '%c' at '%s'\n",data[pos],data.substr(pos).c_str());
    return NULL;
  }
}

DecisionTree::Node *DecisionTree::parseNode(DecisionTree::Type type, std::string data, unsigned long &pos)
{
  DecisionTree::Stats *stats = DecisionTree::parseStats(data,pos);
  if (stats == NULL)
    return NULL;

  if (data.substr(pos,8) == "(WEIGHT[")
  {
    pos += 8;
    float *num = DecisionTree::parseNumber(data,pos);
    if (num == NULL)
    {
      delete stats;
      return NULL;
    }

    if (data.substr(pos,2) != "])")
    {
      fprintf(stderr,"[DT] Error! Expected '])' at '%s'\n",data.substr(pos).c_str());
      delete stats;
      delete num;
      return NULL;
    }
    pos += 2;

    DecisionTree::Node *node = new DecisionTree::Node(stats,*num);
    //fprintf(stderr,"[DT] !!!\n");

    delete num;
    return node;
  }
  else
  {
    if (data[pos] != '(')
    {
      fprintf(stderr,"[DT] Error! Expected '(' at '%s'\n",data.substr(pos).c_str());
      delete stats;
      return NULL;
    }
    pos++;

    std::string label = "";
    while (pos<data.length() && DecisionTree::isText(data[pos]))
    {
      label += data[pos];
      pos++;
    }
    if (label.length() == 0)
    {
      fprintf(stderr,"[DT] Error! Unexpected '%c' at '%s'\n",data[pos],data.substr(pos).c_str());
      delete stats;
      return NULL;
    }

    if (data[pos] != '[')
    {
      fprintf(stderr,"[DT] Error! Expected '[' at '%s'\n",data.substr(pos).c_str());
      delete stats;
      return NULL;
    }
    pos++;

    std::vector<std::string> *attrs = DecisionTree::parseAttrs(data,pos);
    if (attrs == NULL)
    {
      delete stats;
      return NULL;
    }

    std::vector<DecisionTree::Option*> *options = DecisionTree::parseOptions(type,data,pos);
    if (options == NULL)
    {
      delete stats;
      delete attrs;
      return NULL;
    }

    if (data[pos] != ')')
    {
      fprintf(stderr,"[DT] Error! Expected ')' at '%s'\n",data.substr(pos).c_str());
      delete stats;
      return NULL;
    }
    pos++;

    DecisionTree::Query *query = new DecisionTree::Query(label,attrs,options);
    return new DecisionTree::Node(stats,query);
  }
}

DecisionTree::Stats *DecisionTree::parseStats(std::string data, unsigned long &pos)
{
  if (data.substr(pos,7) != "(STATS:")
  {
    fprintf(stderr,"[DT] Error! Expected '(STATS:' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos += 7;

  std::vector<DecisionTree::StatPerm*> *sp;
  if (data[pos] == '(')
  {
    sp = DecisionTree::parseStatPerms(data,pos);
    if (sp == NULL)
      return NULL;
  }
  else
    sp = new std::vector<DecisionTree::StatPerm*>();

  if (data[pos] != ')')
  {
    fprintf(stderr,"[DT] Error! Expected ')' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos++;

  return new DecisionTree::Stats(sp);
}

std::vector<DecisionTree::StatPerm*> *DecisionTree::parseStatPerms(std::string data, unsigned long &pos)
{
  if (data[pos] != '(')
  {
    fprintf(stderr,"[DT] Error! Expected '(' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos++;

  std::string label = "";
  while (pos<data.length() && DecisionTree::isText(data[pos]))
  {
    label += data[pos];
    pos++;
  }
  if (label.length() == 0)
  {
    fprintf(stderr,"[DT] Error! Unexpected '%c' at '%s'\n",data[pos],data.substr(pos).c_str());
    return NULL;
  }

  if (data[pos] != '[')
  {
    fprintf(stderr,"[DT] Error! Expected '[' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos++;

  std::vector<std::string> *attrs = DecisionTree::parseAttrs(data,pos);
  if (attrs == NULL)
    return NULL;

  DecisionTree::Range *range = DecisionTree::parseRange(data,pos);
  if (range == NULL)
  {
    delete attrs;
    return NULL;
  }

  if (data[pos] != ')')
  {
    fprintf(stderr,"[DT] Error! Expected ')' at '%s'\n",data.substr(pos).c_str());
    delete attrs;
    delete range;
    return NULL;
  }
  pos++;

  DecisionTree::StatPerm *sp = new StatPerm(label,attrs,range);
  if (data[pos] == '(')
  {
    std::vector<DecisionTree::StatPerm*> *spstail = DecisionTree::parseStatPerms(data,pos);
    if (spstail == NULL)
    {
      delete sp;
      return NULL;
    }
    std::vector<DecisionTree::StatPerm*> *sps = new std::vector<DecisionTree::StatPerm*>();
    sps->push_back(sp);
    for (unsigned int i=0;i<spstail->size();i++)
    {
      sps->push_back(spstail->at(i));
    }
    delete spstail;

    return sps;
  }
  else
  {
    std::vector<DecisionTree::StatPerm*> *sps = new std::vector<DecisionTree::StatPerm*>();
    sps->push_back(sp);
    return sps;
  }
}

DecisionTree::Range *DecisionTree::parseRange(std::string data, unsigned long &pos)
{
  if (data[pos] != '(')
  {
    fprintf(stderr,"[DT] Error! Expected '(' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos++;

  float *ps = DecisionTree::parseNumber(data,pos);
  if (ps == NULL)
    return NULL;

  if (data[pos] != ':')
  {
    fprintf(stderr,"[DT] Error! Expected ':' at '%s'\n",data.substr(pos).c_str());
    delete ps;
    return NULL;
  }
  pos++;

  float *pe = DecisionTree::parseNumber(data,pos);
  if (pe == NULL)
  {
    delete ps;
    return NULL;
  }

  if (data[pos] != '[')
  {
    fprintf(stderr,"[DT] Error! Expected '[' at '%s'\n",data.substr(pos).c_str());
    delete ps;
    delete pe;
    return NULL;
  }
  pos++;

  float *pv = DecisionTree::parseNumber(data,pos);
  if (pv == NULL)
  {
    delete ps;
    delete pe;
    return NULL;
  }

  if (data[pos] != ']')
  {
    fprintf(stderr,"[DT] Error! Expected ']' at '%s'\n",data.substr(pos).c_str());
    delete ps;
    delete pe;
    delete pv;
    return NULL;
  }
  pos++;

  if (data[pos] == '(')
  {
    DecisionTree::Range *left = DecisionTree::parseRange(data,pos);
    if (left == NULL)
    {
      delete ps;
      delete pe;
      delete pv;
      return NULL;
    }

    DecisionTree::Range *right = DecisionTree::parseRange(data,pos);
    if (right == NULL)
    {
      delete ps;
      delete pe;
      delete pv;
      delete left;
      return NULL;
    }

    if (data[pos] != ')')
    {
      fprintf(stderr,"[DT] Error! Expected ')' at '%s'\n",data.substr(pos).c_str());
      delete ps;
      delete pe;
      delete pv;
      delete left;
      delete right;
      return NULL;
    }
    pos++;

    return new DecisionTree::Range(*ps,*pe,*pv,left,right);
  }
  else
  {
    if (data[pos] != ')')
    {
      fprintf(stderr,"[DT] Error! Expected ')' at '%s'\n",data.substr(pos).c_str());
      delete ps;
      delete pe;
      delete pv;
      return NULL;
    }
    pos++;

    return new DecisionTree::Range(*ps,*pe,*pv);
  }
}

std::vector<DecisionTree::Option*> *DecisionTree::parseOptions(DecisionTree::Type type, std::string data, unsigned long &pos)
{
  std::string label = "";
  while (pos<data.length() && DecisionTree::isText(data[pos]))
  {
    label += data[pos];
    pos++;
  }
  if (label.length() == 0)
  {
    fprintf(stderr,"[DT] Error! Unexpected '%c' at '%s'\n",data[pos],data.substr(pos).c_str());
    return NULL;
  }

  if (data[pos] != ':')
  {
    fprintf(stderr,"[DT] Error! Expected ':' at '%s'\n",data.substr(pos).c_str());
    return NULL;
  }
  pos++;

  DecisionTree::Node *node = DecisionTree::parseNode(type,data,pos);
  if (node == NULL)
    return NULL;

  DecisionTree::Option *opt = new DecisionTree::Option(label,node);
  if (DecisionTree::isText(data[pos]))
  {
    std::vector<DecisionTree::Option*> *optstail = DecisionTree::parseOptions(type,data,pos);
    if (optstail == NULL)
    {
      delete opt;
      return NULL;
    }
    std::vector<DecisionTree::Option*> *opts = new std::vector<DecisionTree::Option*>();
    opts->push_back(opt);
    for (unsigned int i=0;i<optstail->size();i++)
    {
      opts->push_back(optstail->at(i));
    }
    delete optstail;

    return opts;
  }
  else
  {
    std::vector<DecisionTree::Option*> *opts = new std::vector<DecisionTree::Option*>();
    opts->push_back(opt);
    return opts;
  }
}

float *DecisionTree::parseNumber(std::string data, unsigned long &pos)
{
  std::string s = "";
  while (pos<data.length() && (data[pos]=='.' || (data[pos]>='0' && data[pos]<='9')))
  {
    s += data[pos];
    pos++;
  }

  float n;
  try
  {
    n = boost::lexical_cast<float>(s);
  }
  catch (boost::bad_lexical_cast &)
  {
    fprintf(stderr,"[DT] Error! '%s' isn't a valid number\n",s.c_str());
    return NULL;
  }
  float *p = new float();
  *p = n;
  return p;
}

std::list<DecisionTree*> *DecisionTree::loadFile(Parameters *params, std::string filename)
{
  std::ifstream fin(filename.c_str());
  
  if (!fin)
    return NULL;
  
  std::string data = "";
  std::string line;
  while (std::getline(fin,line))
  {
    data += line + "\n";
  }
  
  fin.close();
  
  return DecisionTree::parseString(params,data);
}

bool DecisionTree::saveFile(std::list<DecisionTree*> *trees, std::string filename, bool ignorestats)
{
  std::ofstream fout(filename.c_str());

  if (!fout)
    return false;

  int leafoffset = 0;
  for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
  {
    fout << (*iter)->toString(ignorestats,leafoffset) << "\n\n";
    leafoffset += (*iter)->getLeafCount();
  }

  fout.close();
  return true;
}

bool DecisionTree::isText(char c)
{
  std::string text = TEXT;
  return (text.find(c) != std::string::npos);
}

std::string DecisionTree::stripWhitespace(std::string in)
{
  std::string whitespace = WHITESPACE;
  std::string out = "";
  for (unsigned int i = 0; i < in.length(); i++)
  {
    if (whitespace.find(in[i]) == std::string::npos)
      out += in[i];
  }
  return out;
}

std::string DecisionTree::stripComments(std::string in)
{
  std::string comment = COMMENT;
  std::string newline = "\r\n";
  bool incomment = false;
  std::string out = "";
  for (unsigned int i = 0; i < in.length(); i++)
  {
    if (comment.find(in[i]) != std::string::npos)
      incomment = true;
    else if (newline.find(in[i]) != std::string::npos)
    {
      incomment = false;
      out += in[i];
    }
    else if (!incomment)
      out += in[i];
  }
  return out;
}

DecisionTree::Node::Node(Stats *s, Query *q)
{
  parent = NULL;
  stats = s;
  query = q;
  leafid = -1;
  weight = 0;

  query->setParent(this);
}

DecisionTree::Node::Node(Stats *s, float w)
{
  parent = NULL;
  stats = s;
  query = NULL;
  leafid = -1;
  weight = w;
}

DecisionTree::Node::~Node()
{
  delete stats;
  if (query != NULL)
    delete query;
}

void DecisionTree::Node::populateEmptyStats(DecisionTree::Type type, unsigned int maxnode)
{
  if (stats->getStatPerms()->size() == 0)
  {
    delete stats;
    stats = new DecisionTree::Stats(type,maxnode);
  }

  if (query != NULL)
  {
    std::vector<DecisionTree::Option*> *options = query->getOptions();
    for (unsigned int i=0; i<options->size(); i++)
    {
      DecisionTree::Option *opt = options->at(i);
      bool addnode = false;
      switch (type)
      {
        case STONE:
          if (query->getLabel()=="NEW" && opt->getLabel()!="N")
            addnode = true;
          break;
      }
      opt->getNode()->populateEmptyStats(type,maxnode+(addnode?1:0));
    }
  }
}

void DecisionTree::Node::populateLeafIds(std::vector<DecisionTree::Node*> &leafmap)
{
  if (this->isLeaf())
  {
    leafid = leafmap.size();
    leafmap.push_back(this);;
  }
  else
  {
    std::vector<DecisionTree::Option*> *options = query->getOptions();
    for (unsigned int i=0; i<options->size(); i++)
    {
      options->at(i)->getNode()->populateLeafIds(leafmap);
    }
  }
}

DecisionTree::Stats::Stats(std::vector<StatPerm*> *sp)
{
  statperms = sp;
}

DecisionTree::Stats::Stats(DecisionTree::Type type, unsigned int maxnode)
{
  switch (type)
  {
    case STONE:
      statperms = new std::vector<DecisionTree::StatPerm*>();
      float rangemin = 0;
      float rangemax = 100;

      // ATTR
      for (unsigned int i=0; i<=maxnode; i++)
      {
        if (i>0) // don't need to keep stats on 0'th node
        {
          {
            std::vector<std::string> *attrs = new std::vector<std::string>();
            attrs->push_back("LIB");
            attrs->push_back(boost::lexical_cast<std::string>(i));
            statperms->push_back(new DecisionTree::StatPerm("ATTR",attrs,new DecisionTree::Range(rangemin,rangemax)));
          }
          {
            std::vector<std::string> *attrs = new std::vector<std::string>();
            attrs->push_back("SIZE");
            attrs->push_back(boost::lexical_cast<std::string>(i));
            statperms->push_back(new DecisionTree::StatPerm("ATTR",attrs,new DecisionTree::Range(rangemin,rangemax)));
          }
        }
      }

      // DIST
      for (unsigned int i=0; i<=maxnode; i++)
      {
        for (unsigned int j=i+1; j<=maxnode; j++)
        {
          std::vector<std::string> *attrs = new std::vector<std::string>();
          attrs->push_back(boost::lexical_cast<std::string>(i));
          attrs->push_back(boost::lexical_cast<std::string>(j));
          statperms->push_back(new DecisionTree::StatPerm("DIST",attrs,new DecisionTree::Range(rangemin,rangemax)));
        }

      }

      // NEW
      std::string colslist = "BWS";
      for (unsigned int i=1; i<((unsigned int)1<<colslist.size()); i++)
      {
        std::string cols = "";
        for (unsigned int j=0; j<colslist.size(); j++)
        {
          if ((i>>j)&0x01)
            cols += colslist[j];
        }
        std::vector<std::string> *attrs = new std::vector<std::string>();
        attrs->push_back(cols);
        statperms->push_back(new DecisionTree::StatPerm("NEW",attrs,new DecisionTree::Range(rangemin,rangemax)));
      }

      break;
  }
}

DecisionTree::Stats::~Stats()
{
  for (unsigned int i=0;i<statperms->size();i++)
  {
    delete statperms->at(i);
  }
  delete statperms;
}

DecisionTree::StatPerm::StatPerm(std::string l, std::vector<std::string> *a, Range *r)
{
  label = l;
  attrs = a;
  range = r;
}

DecisionTree::StatPerm::~StatPerm()
{
  delete attrs;
  delete range;
}

DecisionTree::Range::Range(int s, int e, int v, Range *l, Range *r)
{
  start = s;
  end = e;
  val = v;
  parent = NULL;
  left = l;
  right = r;

  left->setParent(this);
  right->setParent(this);
}

DecisionTree::Range::Range(int s, int e, int v)
{
  start = s;
  end = e;
  val = v;
  parent = NULL;
  left = NULL;
  right = NULL;
}

DecisionTree::Range::~Range()
{
  if (left != NULL)
    delete left;
  if (right != NULL)
    delete right;
}

void DecisionTree::Range::addVal(int v, int div)
{
  if (v<start || v>end)
    return;

  if (this->isTerminal() && start!=end && (val>div || this->isRoot()))
  {
    int mid = start + (end - start)/2; //XXX: consider choosing mid according to a log scale
    left = new DecisionTree::Range(start,mid);
    right = new DecisionTree::Range(mid+1,end);
    left->setParent(this);
    right->setParent(this);
  }

  val++;

  if (left!=NULL)
    left->addVal(v,div);
  if (right!=NULL)
    right->addVal(v,div);
}

float DecisionTree::Range::getExpectedMedian(float vl, float vr)
{
  if (left==NULL || right==NULL)
  {
    float t = vl + val + vr;
    float rem = 0.5 - vl/t;
    if (rem <= 0)
      return start;
    else if (rem >= 1)
      return end;
    else
    {
      float p = rem * val/t;
      return start + p*(end-start);
    }
  }
  else
  {
    float s = 1.0f * (left->val + right->val)/val; // scale factor for next level
    float l = s*vl + left->val; // left val relative to next level
    float r = s*vr + right->val; // right val relative to next level
    if (l == r)
      return start + 0.5f*(end-start);
    else if (l > r)
      return left->getExpectedMedian(s*vl,r);
    else
      return right->getExpectedMedian(l,s*vr);
  }
}

float DecisionTree::Range::getExpectedPercentageLessThan(int v)
{
  if (left==NULL || right==NULL)
  {
    if (v <= start)
      return 0;
    else if (v > end)
      return 1;
    else
      return 1.0f * (v-start)/(end-start+1);
  }
  else
  {
    int t = left->val + right->val;
    if (t==0)
      return 1.0f * (v-start)/(end-start+1);

    if (v <= left->end)
    {
      float p = left->getExpectedPercentageLessThan(v);
      return p * left->val/t;
    }
    else if (v >= right->start)
    {
      float p = right->getExpectedPercentageLessThan(v);
      return 1.0f * left->val/t + p * right->val/t;
    }
    else
      return 1.0f * (v-start)/(end-start+1);
  }
}

float DecisionTree::Range::getExpectedPercentageEquals(int v)
{
  if (left==NULL || right==NULL)
  {
    if (v < start)
      return 0;
    else if (v > end)
      return 0;
    else
      return 1.0f/(end-start+1);
  }
  else
  {
    int t = left->val + right->val;
    if (t==0)
      return 1.0f/(end-start+1);

    if (v <= left->end)
    {
      float p = left->getExpectedPercentageEquals(v);
      return p * left->val/t;
    }
    else if (v >= right->start)
    {
      float p = right->getExpectedPercentageEquals(v);
      return p * right->val/t;
    }
    else
      return 1.0f/(end-start+1);
  }
}

DecisionTree::Query::Query(std::string l, std::vector<std::string> *a, std::vector<Option*> *o)
{
  parent = NULL;
  label = l;
  attrs = a;
  options = o;

  for (unsigned int i=0;i<options->size();i++)
  {
    options->at(i)->setParent(this);
  }
}

DecisionTree::Query::Query(DecisionTree::Type type, std::string l, std::vector<std::string> *a, unsigned int maxnode)
{
  parent = NULL;
  label = l;
  attrs = a;

  options = new std::vector<DecisionTree::Option*>();
  switch (type)
  {
    case STONE:
      if (l=="NEW")
      {
        bool B = (attrs->at(0).find('B') != std::string::npos);
        bool W = (attrs->at(0).find('W') != std::string::npos);
        bool S = (attrs->at(0).find('S') != std::string::npos);
        if (B)
          options->push_back(new DecisionTree::Option(type,"B",maxnode+1));
        if (W)
          options->push_back(new DecisionTree::Option(type,"W",maxnode+1));
        if (S)
          options->push_back(new DecisionTree::Option(type,"S",maxnode+1));
        options->push_back(new DecisionTree::Option(type,"N",maxnode));
      }
      else if (l=="DIST" || l=="ATTR")
      {
        options->push_back(new DecisionTree::Option(type,"Y",maxnode));
        options->push_back(new DecisionTree::Option(type,"N",maxnode));
      }
      break;
  }

  for (unsigned int i=0;i<options->size();i++)
  {
    options->at(i)->setParent(this);
  }
}

DecisionTree::Query::~Query()
{
  delete attrs;
  for (unsigned int i=0;i<options->size();i++)
  {
    delete options->at(i);
  }
  delete options;
}

DecisionTree::Option::Option(std::string l, Node *n)
{
  parent = NULL;
  label = l;
  node = n;
  node->setParent(this);
}

DecisionTree::Option::Option(DecisionTree::Type type, std::string l, unsigned int maxnode)
{
  parent = NULL;
  label = l;
  node = new DecisionTree::Node(new DecisionTree::Stats(type,maxnode),1);
  node->setParent(this);
}

DecisionTree::Option::~Option()
{
  delete node;
}

DecisionTree::StoneGraph::StoneGraph(Go::Board *board)
{
  this->board = board;
  nodes = new std::vector<DecisionTree::StoneGraph::StoneNode*>();
  edges = new std::vector<std::vector<int>*>();

  for (int p = 0; p < board->getPositionMax(); p++)
  {
    if (board->inGroup(p))
    {
      DecisionTree::StoneGraph::StoneNode *node = new DecisionTree::StoneGraph::StoneNode();

      node->pos = p;
      node->col = board->getColor(p);
      Go::Group *group = board->getGroup(p);
      node->size = group->numOfStones();
      if (group->inAtari())
        node->liberties = 1;
      else
        node->liberties = group->numOfPseudoLiberties();

      nodes->push_back(node);
    }
  }

  for (int i = -1; i >= -4; i--)
  {
    DecisionTree::StoneGraph::StoneNode *node = new DecisionTree::StoneGraph::StoneNode();

    node->pos = i;
    node->col = Go::OFFBOARD;
    node->size = 0;
    node->liberties = 0;

    nodes->push_back(node);
  }

  unsigned int N = nodes->size();
  for (unsigned int i=0; i<N; i++)
  {
    edges->push_back(new std::vector<int>());

    int p1 = nodes->at(i)->pos;
    for (unsigned int j=0; j<i; j++)
    {
      int p2 = nodes->at(j)->pos;
      int d = DecisionTree::getDistance(board,p1,p2);
      edges->at(i)->push_back(d);
    }
  }

  auxnode = -1;
}

DecisionTree::StoneGraph::~StoneGraph()
{
  for (unsigned int i = 0; i < nodes->size(); i++)
  {
    delete nodes->at(i);
    delete edges->at(i);
  }
  delete nodes;
  delete edges;
}

std::string DecisionTree::StoneGraph::toString()
{
  std::ostringstream ss;

  for (unsigned int i = 0; i < (3+1 + 1+1 + 2+1 + 2+3); i++)
    ss << " ";
  for (unsigned int i = 0; i < this->getNumNodes(); i++)
    ss << " " << std::setw(3)<<i;
  ss << "\n";

  for (unsigned int i = 0; i < this->getNumNodes(); i++)
  {
    ss << std::setw(3)<<i << " " << Go::colorToChar(this->getNodeStatus(i)) << " " << std::setw(2)<<this->getNodeSize(i) << " " << std::setw(2)<<this->getNodeLiberties(i) << "   ";
  
    for (unsigned int j = 0; j < this->getNumNodes(); j++)
    {
      if (i==j)
        ss << "   -";
      else
        ss << " " << std::setw(3)<<this->getEdgeWeight(i,j);
    }

    ss << "\n";
  }

  return ss.str();
}

int DecisionTree::StoneGraph::getEdgeWeight(unsigned int node1, unsigned int node2)
{
  if (node1 == node2)
    return 0;
  else if (node1 < node2)
    return edges->at(node2)->at(node1);
  else
    return edges->at(node1)->at(node2);
};

unsigned int DecisionTree::StoneGraph::addAuxNode(int pos)
{
  if (auxnode != (unsigned int)-1)
    throw "Aux node already present";

  DecisionTree::StoneGraph::StoneNode *node = new DecisionTree::StoneGraph::StoneNode();

  node->pos = pos;
  node->col = Go::EMPTY;
  node->size = 0;
  node->liberties = 0;

  nodes->push_back(node);
  edges->push_back(new std::vector<int>());

  unsigned int N = nodes->size();
  unsigned int i = N - 1;

  for (unsigned int j=0; j<i; j++)
  {
    int p2 = nodes->at(j)->pos;
    int d = DecisionTree::getDistance(board,pos,p2);
    edges->at(i)->push_back(d);
  }

  auxnode = i;

  return i;
}

void DecisionTree::StoneGraph::removeAuxNode()
{
  if (auxnode == (unsigned int)-1)
    throw "Aux node not present";

  // assume that the last node added is the aux node
  unsigned int N = nodes->size();
  unsigned int i = N - 1;

  delete nodes->at(i);
  delete edges->at(i);

  nodes->resize(N-1);
  edges->resize(N-1);

  auxnode = -1;
}

std::list<unsigned int> *DecisionTree::StoneGraph::getSortedNodesFromAux()
{
  throw "TODO";
  return NULL;
}

int DecisionTree::StoneGraph::compareNodes(unsigned int node1, unsigned int node2, unsigned int ref)
{
  throw "TODO";
  return 0;
}

void DecisionTree::StoneGraph::compressChain()
{
  bool change = true;
  while (change)
  {
    change = false;
    for (unsigned int i=0; i<this->getNumNodes(); i++)
    {
      Go::Color col = this->getNodeStatus(i);
      if (col==Go::BLACK || col==Go::WHITE)
      {
        for (unsigned int j=i+1; j<this->getNumNodes(); j++)
        {
          int d = this->getEdgeWeight(i,j);
          if (d==1 && this->getNodeStatus(i)==this->getNodeStatus(j))
          {
            this->mergeNodes(i,j);
            change = true;
            j--; // j was removed by the merge
            continue;
          }
        }
      }
    }
  }
}

void DecisionTree::StoneGraph::mergeNodes(unsigned int n1, unsigned int n2)
{
  if (n1==n2)
    return;
  else if (n2<n1)
  {
    this->mergeNodes(n2,n1);
    return;
  }

  // No change to node1 required
  // DecisionTree::StoneGraph::StoneNode *node1 = nodes->at(n1);
  // DecisionTree::StoneGraph::StoneNode *node2 = nodes->at(n2);
  // int pos = node1->pos;
  // Go::Color col = node1->col;
  // int size = node1->size; // no reduction required
  // int liberties = node1->liberties; // no reduction required
  
  // Merge edges of n1 and n2
  for (unsigned int i=0; i<n1; i++)
  {
    int dist1 = edges->at(n1)->at(i);
    int dist2 = edges->at(n2)->at(i);

    int mindist = (dist1<dist2?dist1:dist2);

    edges->at(n1)->at(i) = mindist;
  }

  // Merge edges between n1 and n2
  for (unsigned int i=n1+1; i<n2; i++)
  {
    int dist1 = edges->at(i)->at(n1);
    int dist2 = edges->at(n2)->at(i);

    int mindist = (dist1<dist2?dist1:dist2);

    edges->at(i)->at(n1) = mindist;
  }

  // Merge edges after n2
  for (unsigned int i=n2+1; i<nodes->size(); i++)
  {
    int dist1 = edges->at(i)->at(n1);
    int dist2 = edges->at(i)->at(n2);

    int mindist = (dist1<dist2?dist1:dist2);

    edges->at(i)->at(n1) = mindist;
    edges->at(i)->erase(edges->at(i)->begin()+n2);
  }

  delete nodes->at(n2);
  nodes->erase(nodes->begin()+n2);

  delete edges->at(n2);
  edges->erase(edges->begin()+n2);
}

DecisionTree::GraphCollection::GraphCollection(Go::Board *board)
{
  this->board = board;

  stoneNone = new DecisionTree::StoneGraph(board);

  stoneChain = new DecisionTree::StoneGraph(board);
  stoneChain->compressChain();

  // fprintf(stderr,"stoneNone:\n%s\n",stoneNone->toString().c_str());
  // fprintf(stderr,"stoneChain:\n%s\n",stoneChain->toString().c_str());
}

DecisionTree::GraphCollection::~GraphCollection()
{
  delete stoneNone;
  delete stoneChain;
}

DecisionTree::StoneGraph *DecisionTree::GraphCollection::getStoneGraph(bool compressChain)
{
  if (compressChain)
    return stoneChain;
  else
    return stoneNone;
}

