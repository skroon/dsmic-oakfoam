#ifndef DEF_OAKFOAM_UCT_H
#define DEF_OAKFOAM_UCT_H

#include <cmath>
#include <list>
#include "Go.h"

namespace UCT
{ 
  class Tree
  {
    public:
      Tree(float uc, float ui, int rm, Go::Move mov = Go::Move(Go::EMPTY,Go::Move::RESIGN), UCT::Tree *p = NULL);
      ~Tree();
      
      UCT::Tree *getParent() { return parent; };
      std::list<UCT::Tree*> *getChildren() { return children; };
      Go::Move getMove() { return move; };
      bool isRoot() { return (parent==NULL); };
      bool isLeaf() { return (children->size()==0); };
      bool isTerminal();
      std::list<Go::Move> getMovesFromRoot();
      void divorceChild(UCT::Tree *child);
      
      UCT::Tree *getChild(Go::Move move);
      int getPlayouts() { return playouts; };
      int getRAVEPlayouts() { return raveplayouts; };
      float getRatio();
      float getRAVERatio();
      float getVal();
      float getUrgency();
      
      void addChild(UCT::Tree *node);
      void addWin();
      void addLose();
      void addRAVEWin();
      void addRAVELose();
      
    private:
      UCT::Tree *parent;
      std::list<UCT::Tree*> *children;
      
      Go::Move move;
      int playouts,raveplayouts;
      int wins,ravewins;
      int ravemoves;
      float ucbc,ucbinit;
      
      void passPlayoutUp(bool win);
  };
};
#endif