#include "Tree.h"

#include <cmath>
#include <sstream>
#include "Parameters.h"
#include "Engine.h"
#include "Pattern.h"
#include "Worker.h"

Tree::Tree(Parameters *prms, Go::ZobristHash h, Go::Move mov, Tree *p) : params(prms)
{
  params->tree_instances++;
  parent=p;
  children=new std::list<Tree*>();
  beenexpanded=false;
  move=mov;
  playouts=0;
  wins=0;
  scoresum=0;
  scoresumsq=0;
  raveplayouts=0;
  earlyraveplayouts=0;
  ravewins=0;
  earlyravewins=0;
  raveplayoutsother=0;
  ravewinsother=0;
  earlyraveplayoutsother=0;
  earlyravewinsother=0;
  
  symmetryprimary=NULL;
  hasTerminalWinrate=false;
  hasTerminalWin=false;
  terminaloverride=false;
  pruned=false;
  prunedchildren=0;
  gamma=0;
  childrentotalgamma=0;
  maxchildgamma=0;
  unprunenextchildat=0;
  lastunprune=0;
  unprunebase=0;
  unprunedchildren=0;
  hash=h;
  ownedblack=0;
  ownedwhite=0;
  ownedwinner=0;
  biasbonus=0;
  superkoprunedchildren=false;
  superkoviolation=false;
  superkochecked=false;
  superkochildrenviolations=0;
  decayedwins=0;
  decayedplayouts=0;
  unprunednum=0;
  
  #ifdef HAVE_MPI
    this->resetMpiDiff();
  #endif
  
  if (parent!=NULL)
  {
    Go::Move pmove=parent->getMove();
    if (!pmove.isPass() && !pmove.isResign())
    {
      if (pmove.getColor()==move.getColor())
        fprintf(stderr,"WARNING! unexpected parent color\n");
    }
  }
}

Tree::~Tree()
{
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    delete (*iter);
  }
  children->clear();
  delete children;
  params->tree_instances--;
}

void Tree::addChild(Tree *node)
{
  if (move.getColor()==node->move.getColor() && (children->size()==0 || children->back()->move.getColor()!=node->move.getColor()))
    fprintf(stderr,"WARNING! Expecting alternating colors in tree\n");
  
  children->push_back(node);
  node->parent=this;
}

float Tree::getScoreMean() const
{
  if (playouts>0)
  {
    if (move.getColor()==Go::BLACK)
      return scoresum/playouts;
    else
      return -scoresum/playouts;
  }
  else
    return 0;
}

float Tree::getScoreSD() const
{
  if (playouts>0 && (scoresumsq/playouts-pow(scoresum/playouts,2))>0)
    return sqrt((scoresumsq/playouts-pow(scoresum/playouts,2))/playouts);
  else
    return 0;
}

float Tree::getRatio() const
{
  if (this->isTerminalResult())
    return (hasTerminalWin?1:0);
  else if (playouts>0)
  {
    if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
      return decayedwins/decayedplayouts;
    else
      return (float)wins/playouts;
  }
  else
    return 0;
}

float Tree::getRAVERatio() const
{
  //passes and resigns do not have a correct rave value
  if (raveplayouts>0 && getMove().isNormal())
    return (float)ravewins/raveplayouts;
  else
    return 0;
}

float Tree::getEARLYRAVERatio() const
{
  //passes and resigns do not have a correct rave value
  if (earlyraveplayouts>0 && getMove().isNormal())
    return (float)earlyravewins/earlyraveplayouts;
  else
    return 0;
}

float Tree::getRAVERatioOther() const
{
  //passes and resigns do not have a correct rave value
  if (raveplayoutsother>0 && getMove().isNormal())
    return (float)ravewinsother/raveplayoutsother;
  else
    return 0;
}

float Tree::getRAVERatioForPool() const
{
  if (raveplayouts>0)
    return (float)(ravewins-params->rave_init_wins)/(raveplayouts-params->rave_init_wins+1);
  else
    return 0;
}

float Tree::getRAVERatioOtherForPool() const
{
  if (raveplayoutsother>0)
    return (float)(ravewinsother-params->rave_init_wins)/(raveplayoutsother-params->rave_init_wins+1);
  else
    return 0;
}

void Tree::addWin(int fscore, Tree *source)
{
  boost::mutex::scoped_lock *lock=(params->uct_lock_free?NULL:new boost::mutex::scoped_lock(updatemutex));
  /*if (this->isTerminalResult())
  {
    delete lock;
    return;
  }*/
  
  wins++;
  playouts++;
  scoresum+=fscore;
  scoresumsq+=fscore*fscore;
  if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
    this->addDecayResult(1);
  
  if (lock!=NULL)
    delete lock;
  this->passPlayoutUp(fscore,true,source);
  this->checkForUnPruning();
}

void Tree::addLose(int fscore, Tree *source)
{
  boost::mutex::scoped_lock *lock=(params->uct_lock_free?NULL:new boost::mutex::scoped_lock(updatemutex));
  /*if (this->isTerminalResult())
  {
    delete lock;
    return;
  }*/

  playouts++;
  scoresum+=fscore;
  scoresumsq+=fscore*fscore;
  if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
    this->addDecayResult(0);
  
  if (lock!=NULL)
    delete lock;
  this->passPlayoutUp(fscore,false,source);
  this->checkForUnPruning();
}

void Tree::addDecayResult(float result)
{
  if (params->uct_decay_alpha!=1)
  {
    decayedwins*=params->uct_decay_alpha;
    decayedplayouts*=params->uct_decay_alpha;
  }
  if (params->uct_decay_k!=0 && playouts>0)
  {
    float decayfactor=pow((playouts+params->uct_decay_m-1)/(playouts+params->uct_decay_m),params->uct_decay_k);
    //fprintf(stderr,"decayfactor: %.6f\n",decayfactor);
    decayedwins*=decayfactor;
    decayedplayouts*=decayfactor;
  }
  decayedwins+=result;
  decayedplayouts++;
}

void Tree::addVirtualLoss()
{
  boost::mutex::scoped_lock *lock=(params->uct_lock_free?NULL:new boost::mutex::scoped_lock(updatemutex));
  playouts++;
  if (lock!=NULL)
    delete lock;
}

void Tree::removeVirtualLoss()
{
  boost::mutex::scoped_lock *lock=(params->uct_lock_free?NULL:new boost::mutex::scoped_lock(updatemutex));
  playouts--;
  if (lock!=NULL)
    delete lock;
  if (!this->isRoot() && !parent->isRoot())
    parent->removeVirtualLoss();
}

void Tree::addPriorWins(int n)
{
  wins+=n;
  playouts+=n;
  if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
  {
    for (int i=0;i<n;i++)
    {
      this->addDecayResult(1);
    }
  }
}

void Tree::addPriorLoses(int n)
{
  playouts+=n;
  if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
  {
    for (int i=0;i<n;i++)
    {
      this->addDecayResult(0);
    }
  }
}

void Tree::addRAVEWin(bool early)
{
  //boost::mutex::scoped_lock lock(updatemutex);
  if (early)
  {
    earlyravewins++;
    earlyraveplayouts++;
  }
  else
  {
    ravewins++;
    raveplayouts++;
  }
}

void Tree::addRAVELose(bool early)
{
  //boost::mutex::scoped_lock lock(updatemutex);
  if (early)
  {
    earlyraveplayouts++;
  }
  else
  {
    raveplayouts++;
  }
}

void Tree::addRAVEWinOther(bool early)
{
  //boost::mutex::scoped_lock lock(updatemutex);
  if (early) 
  {
    earlyravewinsother++;
    earlyraveplayoutsother++;
  }
  else
  {
    ravewinsother++;
    raveplayoutsother++;
  }
}

void Tree::addRAVELoseOther(bool early)
{
  //boost::mutex::scoped_lock lock(updatemutex);
  if (early)
    earlyraveplayoutsother++;
  else
    raveplayoutsother++;
}

void Tree::addRAVEWins(int n,bool early)
{
  if (early)
  {
//  earlyravewins+=n;
//  earlyraveplayouts+=n;

//  earlyravewinsother+=n;
//  earlyraveplayoutsother+=n;
  }
  else
  {
    ravewins+=n;
    raveplayouts+=n;

    //only used for initialization, therefore both can be done in this function
    ravewinsother+=n;
    raveplayoutsother+=n;
  }
}  


void Tree::addRAVELoses(int n,bool early)
{
  if (early)
  {
    earlyraveplayouts+=n;
  }
  else
  {
    raveplayouts+=n;
  }
}

void Tree::addPartialResult(float win, float playout, bool invertwin)
{
  boost::mutex::scoped_lock *lock=(params->uct_lock_free?NULL:new boost::mutex::scoped_lock(updatemutex));
  //fprintf(stderr,"adding partial result: %f %f %d\n",win,playout,invertwin);
  if (!this->isTerminalResult())
  {
    wins+=win;
    playouts+=playout;
  }
  if (lock!=NULL)
    delete lock;
  if (!this->isRoot())
  {
    if (invertwin)
      parent->addPartialResult(-win,playout,invertwin);
    else
      parent->addPartialResult(1-win,playout,invertwin);
  }
  this->checkForUnPruning();
}

Tree *Tree::getChild(Go::Move move) const
{
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if ((*iter)->getMove()==move && !(*iter)->isSuperkoViolation()) 
      return (*iter);
  }
  return NULL;
}

bool Tree::allChildrenTerminalLoses()
{
  //fprintf(stderr,"check if all children of %s are losses: ",move.toString(params->board_size).c_str());
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if ((*iter)->isPrimary() && !(*iter)->isTerminalLose())
    {
      //fprintf(stderr,"false\n");
      return false;
    }
  }
  //fprintf(stderr,"true (%d)\n",children->size());
  return true;
}

bool Tree::hasOneUnprunedChildNotTerminalLoss()
{
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if (!(*iter)->isPruned() && !(*iter)->isTerminalLose())
      return true;
  }
  return false;
}

void Tree::passPlayoutUp(int fscore, bool win, Tree *source)
{
  if (params->uct_terminal_handling)
  {
    bool passterminal=(!this->isTerminalResult()) && (this->isTerminal() || (source!=NULL && source->isTerminalResult()));
    
    if (passterminal)
    {
      boost::mutex::scoped_lock *lock=(params->uct_lock_free?NULL:new boost::mutex::scoped_lock(updatemutex));
      if (!this->isTerminalResult())
      {
        if (source!=NULL)
          win=!source->isTerminalWin();
        if (!win || this->allChildrenTerminalLoses())
        {
          //fprintf(stderr,"Terminal info: (%d -> %d)(%d,%d)(%p,%d,%d,%s)\n",hasTerminalWin,win,hasTerminalWinrate,this->isTerminalResult(),source,(source!=NULL?source->isTerminalResult():0),(source!=NULL?source->hasTerminalWin:0),(source!=NULL?source->getMove().toString(params->board_size).c_str():""));
          hasTerminalWin=win;
          hasTerminalWinrate=true;
          if (params->debug_on)
            params->engine->getGtpEngine()->getOutput()->printfDebug("New Terminal Result %d! (%s)\n",win,move.toString(params->board_size).c_str());
          if (!win && !this->isRoot() && parent->hasPrunedChildren())
          {
            parent->unPruneNow();
            while (parent->hasPrunedChildren() && !parent->hasOneUnprunedChildNotTerminalLoss())
              parent->unPruneNow(); //unprune a non-terminal loss
          }
        }
      }
      if (lock!=NULL)
        delete lock;
    }
  }
  else if (this->isTerminal() && !this->isTerminalResult())
  {
    hasTerminalWin=win;
    hasTerminalWinrate=true;
  }
  
  if (!this->isRoot())
  {
    //assume alternating colours going up
    if (win)
      parent->addLose(fscore,this);
    else
      parent->addWin(fscore,this);
  }
}

float Tree::getVal(bool skiprave) const
{
  if (this->isTerminalResult())
    return (hasTerminalWin?1:0);
  else if (this->isTerminal())
    return 1;
  if (params->rave_moves==0 || raveplayouts==0 || skiprave)
    return this->getRatio();
  else if (playouts==0)
    return this->getRAVERatio();
  else
  {
    //float alpha=(float)(ravemoves-playouts)/params->rave_moves;

    float alpha=(float)raveplayouts/(raveplayouts + playouts + (float)(raveplayouts*playouts)/params->rave_moves);
    //float alpha=exp(-pow((float)playouts/params->rave_moves,params->test_p1));
    //float alpha=params->test_p2/(exp(pow((float)playouts/params->rave_moves,params->test_p1))+params->test_p2);
    //if (alpha<0.5)
    //  fprintf(stderr,"%5.0f %5.0f %5.2f\n",raveplayouts,playouts,alpha);
    //float alpha=exp(-pow((float)playouts/params->rave_moves,params->test_p1));
    //float alpha=1.0/pow(playouts*params->test_p6+params->test_p7,params->test_p8);
    if (alpha<=0)
      return this->getRatio();
    else if (alpha>=1)
      return this->getRAVERatio();
    else
      //return this->getEARLYRAVERatio()*alpha + this->getRatio()*(1-alpha);
      return this->getRAVERatio()*alpha + this->getRatio()*(1-alpha);
  }
}

float Tree::getUrgency(bool skiprave) const
{
  float uctbias;
  if (this->isTerminal())
  {
    if (this->isTerminalWin() || !this->isTerminalResult())
      return TREE_TERMINAL_URGENCY;
    else
      return -TREE_TERMINAL_URGENCY;
  }
  
  if (playouts==0 && raveplayouts==0)
    return params->ucb_init;

  if (parent==NULL || params->ucb_c==0)
    uctbias=0;
  else
  {
    if (parent->getPlayouts()>1 && playouts>0)
      uctbias=params->ucb_c*sqrt(log((float)parent->getPlayouts())/(playouts));
    else if (parent->getPlayouts()>1)
      uctbias=params->ucb_c*sqrt(log((float)parent->getPlayouts())/(1));
    else
      uctbias=params->ucb_c/2;
  }

  if (params->bernoulli_a>0)
    uctbias+=params->bernoulli_a*exp(-params->bernoulli_b*playouts);

  float val=this->getVal(skiprave)+uctbias;
  if (params->weight_score>0)
    val+=params->weight_score*this->getScoreMean();

  if (params->uct_progressive_bias_enabled)
    val+=this->getProgressiveBias();
  
  if (params->uct_criticality_urgency_factor>0 && (params->uct_criticality_siblings?parent->playouts:playouts)>(params->uct_criticality_min_playouts))
  {
    if (params->uct_criticality_urgency_decay>0.0)
      val+=params->uct_criticality_urgency_factor*this->getCriticality()*exp(-(float)playouts*params->uct_criticality_urgency_decay);// /pow((playouts+1),params->uct_criticality_urgency_decay); //added log 01.1.2013
    else
      val+=params->uct_criticality_urgency_factor*this->getCriticality();
  }
  
  return val;
}

std::list<Go::Move> Tree::getMovesFromRoot() const
{
  if (this->isRoot())
    return std::list<Go::Move>();
  else
  {
    std::list<Go::Move> list=parent->getMovesFromRoot();
    list.push_back(this->getMove());
    return list;
  }
}

bool Tree::isTerminal() const
{
  if (terminaloverride)
    return false;
  else if (hasTerminalWinrate)
    return true;
  else if (!this->isRoot())
  {
    if (this->getMove().isPass() && parent->getMove().isPass())
      return true;
    else
      return false;
  }
  else
    return false;
}

void Tree::divorceChild(Tree *child)
{
  children->remove(child);
  child->parent=NULL;
}

float Tree::variance(int wins, int playouts)
{
  return wins-(float)(wins*wins)/playouts;
}

std::string Tree::toSGFString() const
{
  std::ostringstream ss;
  if (!this->isRoot())
  {
    ss<<"(;"<<Go::colorToChar(move.getColor())<<"[";
    if (!move.isPass()&&!move.isResign())
    {
      ss<<(char)(move.getX(params->board_size)+'a');
      ss<<(char)(params->board_size-move.getY(params->board_size)+'a'-1);
    }
    else if (move.isPass())
      ss<<"pass";
    ss<<"]C[";
    if (this->isTerminalWin())
      ss<<"Terminal Win ("<<wins<<","<<hasTerminalWin<<")\n";
    else if (this->isTerminalLose())
      ss<<"Terminal Lose ("<<wins<<","<<hasTerminalWin<<")\n";
    else
    {
      ss<<"Wins/Playouts: "<<wins<<"/"<<playouts<<"("<<this->getRatio()<<")\n";
      if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
        ss<<"Decayed Playouts: "<<decayedplayouts<<"\n";
      ss<<"RAVE Wins/Playouts: "<<ravewins<<"/"<<raveplayouts<<"("<<this->getRAVERatio()<<")\n";
      ss<<"Score Mean: "<<scoresum<<"/"<<playouts<<"("<<this->getScoreMean()<<")\n";
      ss<<"Score SD: "<<this->getScoreSD()<<"\n";
    }
    ss<<"Urgency: "<<this->getUrgency()<<"\n";
    if (!this->isLeaf())
      ss<<"Pruned: "<<prunedchildren<<"/"<<children->size()<<"("<<(children->size()-prunedchildren)<<")\n";
    else if (this->isTerminal())
      ss<<"Terminal Node\n";
    else
      ss<<"Leaf Node\n";
    ss<<"]";
  }
  else
  {
    ss<<"C[";
    if (!move.isResign())
      ss<<"Last Move: "<<move.toString(params->board_size)<<"\n";
    ss<<"Children: "<<children->size()<<"\n";
    if (this->isTerminalWin())
      ss<<"Terminal Win\n";
    else if (this->isTerminalLose())
      ss<<"Terminal Lose\n";
    if (this->isSuperkoViolation())
      ss<<"Superko Violation!\n";
    ss<<"Wins/Playouts: "<<wins<<"/"<<playouts<<"("<<this->getRatio()<<")\n";
    ss<<"Score Mean: "<<scoresum<<"/"<<playouts<<"("<<this->getScoreMean()<<")\n";
    ss<<"Score SD: "<<"("<<this->getScoreSD()<<")\n";
    if (params->uct_decay_alpha!=1 || params->uct_decay_k!=0)
        ss<<"Decayed Playouts: "<<decayedplayouts<<"\n";
    ss<<"]";
  }
  
  Tree *bestchild=NULL;
  int besti=0;
  bool usedchild[children->size()];
  for (unsigned int i=0;i<children->size();i++)
    usedchild[i]=false;
  int childrendone=0;
  
  while (true)
  {
    int i=0;
    for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
    {
      if (usedchild[i]==false && ((*iter)->getPlayouts()>0 || (*iter)->isTerminal() || (params->outputsgf_maxchildren==0 && (*iter)->isPrimary())) && !(*iter)->isSuperkoViolation())
      {
        if (bestchild==NULL || (*iter)->getPlayouts()>bestchild->getPlayouts() || (*iter)->isTerminalWin())
        {
          bestchild=(*iter);
          besti=i;
          if ((*iter)->isTerminalWin())
            break;
        }
      }
      i++;
    }
    if (bestchild==NULL)
      break;
    
    ss<<bestchild->toSGFString();
    usedchild[besti]=true;
    bestchild=NULL;
    besti=0;
    childrendone++;
    if (params->outputsgf_maxchildren!=0 && childrendone>=params->outputsgf_maxchildren)
      break;
  }
  if (!this->isRoot())
    ss<<")\n";
  return ss.str();
}

void Tree::performSymmetryTransform(Go::Board::SymmetryTransform trans)
{
  if (!move.isPass() && !move.isResign())
    move.setPosition(Go::Board::doSymmetryTransformStaticReverse(trans,params->board_size,move.getPosition()));
  
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    (*iter)->performSymmetryTransform(trans);
  }
}

void Tree::performSymmetryTransformParentPrimary()
{
  if (!this->isRoot() && !this->isPrimary())
  {
    Go::Board::SymmetryTransform trans=Go::Board::getSymmetryTransformBetweenPositions(params->board_size,move.getPosition(),symmetryprimary->getMove().getPosition());
    //fprintf(stderr,"transform: ix:%d iy:%d sxy:%d\n",trans.invertX,trans.invertY,trans.swapXY);
    parent->performSymmetryTransform(trans);
  }
}

void Tree::pruneChildren()
{
  prunedchildren=0;
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if ((*iter)->isPrimary())
    {
      (*iter)->setPruned(true);
      prunedchildren++;
    }
  }
  unprunedchildren=0;
}

Tree *Tree::getWorstChild()
{
  //fprintf(stderr,"+");
  Tree *worstchild=NULL;
  int unprunedn=0;
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    //fprintf(stderr,".");
    if ((*iter)->isPrimary() && !(*iter)->isPruned())
    {
      if (worstchild==NULL || (*iter)->getPlayouts()<worstchild->getPlayouts())
        worstchild=(*iter);
      unprunedn++;
    }
  }
  if (unprunedn<2)
    return NULL;
  return worstchild;
}

void Tree::unPruneNextChildNew()
{
  if (params->uct_reprune_factor>0.0)
  {
    Tree* worstChild=NULL;
    worstChild=getWorstChild();
    if (worstChild)
    {
      if (this->isRoot())
      {
      //  Gtp::Engine *gtpe=params->engine->getGtpEngine();
      //  gtpe->getOutput()->printfDebug("wc:%s %5.3f %5.3f (%5.0f) %5.3f\n",worstChild->getMove().toString(params->board_size).c_str(),worstChild->getUnPruneFactor(),worstChild->getRAVERatio(),worstChild->getRAVEPlayouts(),worstChild->getFeatureGamma());
      }
      worstChild->setPruned(true);
      unprunedchildren--;
      unPruneNextChild();
    }
  }
  unPruneNextChild();
}

void Tree::unPruneNextChild()
{
//  Gtp::Engine *gtpe=params->engine->getGtpEngine();
  if (this->hasPrunedChildren())
  {
    Tree *bestchild=NULL;
    float bestfactor=-1;
    unsigned int unpruned=0;

    //moves of the same color two levels higher in the tree
    Tree *higher=this->getParent();
    if (higher) higher=higher->getParent();
    //if (higher) higher=higher->getParent();
    float *moveValues=NULL;
    int num=0;
    float sum=0;
    float mean=0;
    if (higher && params->uct_oldmove_unprune_factor_b>0.0)
    {
      //allocated here, as it would be a lot of memory if kept on every tree node!!
      moveValues=new float[params->engine->getCurrentBoard()->getPositionMax()];
      for (int i=0;i<params->engine->getCurrentBoard()->getPositionMax();i++)
        moveValues[i]=0;
      std::list<Tree*> *childrenTmp=higher->getChildren();
      for(std::list<Tree*>::iterator iter=childrenTmp->begin();iter!=childrenTmp->end();++iter) 
      { 
        if (!(*iter)->isPruned() && ((*iter)->getMove()).isNormal())
        {
          num++;
          sum+=(*iter)->getRatio();
          moveValues[((*iter)->getMove()).getPosition()]=(*iter)->getRatio();
        }
      }
      if (num>0)
        mean=sum/num;
    }
    //float unprune_select_p=(float)rand()/RAND_MAX;
    for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
    {
      if ((*iter)->isPrimary() && !(*iter)->isSuperkoViolation())
      {
        if ((*iter)->isPruned())
        {
          if ((*iter)->getUnPruneFactor()>bestfactor || bestchild==NULL)
          {
            bestfactor=(*iter)->getUnPruneFactor(moveValues,mean,num);
            bestchild=(*iter);
          }
          //  fprintf(stderr,"%s %5.3f %5.3f (%5.0f) %5.3f",(*iter)->getMove().toString(params->board_size).c_str(),(*iter)->getUnPruneFactor(),(*iter)->getRAVERatio(),(*iter)->getRAVEPlayouts(),(*iter)->getFeatureGamma());
          // Following is commented out because is cannot be disabled
          /*if (unprune_select_p<params->test_p9 || this->getNumUnprunedChildren()<3)
          {
            if ((*iter)->getUnPruneFactor()>bestfactor || bestchild==NULL)
            {
              bestfactor=(*iter)->getUnPruneFactor(moveValues,mean,num);
              bestchild=(*iter);
            }
          }
          else
          {
            if (unprune_select_p<(1.0-params->test_p9)*params->test_p10+params->test_p9)
            {
              if ((*iter)->getRAVERatio()>bestfactor || bestchild==NULL)
              {
                bestfactor=(*iter)->getRAVERatio();
                bestchild=(*iter);
              }
            }
            else
            {
              if ((*iter)->getCriticality()>bestfactor || bestchild==NULL)
              {
                bestfactor=(*iter)->getCriticality();
                bestchild=(*iter);
              }
            }
          }*/
        }
        else
        {
          unpruned++;
          //if (this->isRoot())
          //  fprintf(stderr,"%s %5.3f %5.3f (%5.0f) %5.3f ",(*iter)->getMove().toString(params->board_size).c_str(),(*iter)->getUnPruneFactor(),(*iter)->getRAVERatio(),(*iter)->getRAVEPlayouts(),(*iter)->getFeatureGamma());
        }
      }
    }
    //if (this->isRoot())
    //  fprintf(stderr,"\n");
    
    if (bestchild!=NULL)
    {
      //if (unpruned>2)
      //  fprintf(stderr,"\n[unpruning]: (%d) %s bestfactor %f Rave %f EarlyRave %f Criticality %f -- %f\n\n",unpruned,bestchild->getMove().toString(params->board_size).c_str(),bestfactor,bestchild->getRAVERatio (),bestchild->getEARLYRAVERatio (),bestchild->getCriticality(), bestchild->getUnPruneFactor ());
      bestchild->setPruned(false);
      //if (this->isRoot() && params->uct_reprune_factor>0.0)
      //      gtpe->getOutput()->printfDebug("nc:%s %5.3f %5.3f (%5.0f) %5.3f\n",bestchild->getMove().toString(params->board_size).c_str(),bestchild->getUnPruneFactor(),bestchild->getRAVERatio(),bestchild->getRAVEPlayouts(),bestchild->getFeatureGamma());
      if ((unpruned+superkochildrenviolations)!=unprunedchildren)
        fprintf(stderr,"WARNING! unpruned running total doesn't match (%u:%u)\n",unpruned,unprunedchildren);
      bestchild->setUnprunedNum(unpruned+1);
      unprunedchildren++;
      unprunebase=params->uct_progressive_widening_a*pow(params->uct_progressive_widening_b,unpruned);
      lastunprune=this->unPruneMetric();
      this->updateUnPruneAt();
      prunedchildren--;
    }
    if (moveValues) delete moveValues;
  }
}

float Tree::unPruneMetric() const
{
  if (params->uct_progressive_widening_count_wins)
    return wins;
  else
    return playouts;
}

void Tree::updateUnPruneAt()
{
  float scale;
  if (playouts>0)
    scale=(1-params->uct_progressive_widening_c*this->getRatio());
  else
    scale=1;
  unprunenextchildat=lastunprune+unprunebase*scale; //t(n+1)=t(n)+(a*b^n)*(1-c*p)
}

void Tree::checkForUnPruning()
{
  this->updateUnPruneAt();
  if (this->unPruneMetric()>=unprunenextchildat)
  {
    //boost::mutex::scoped_lock lock(unprunemutex);
    //unprunemutex.lock();
    if (!unprunemutex.try_lock())
      return;
    if (this->unPruneMetric()>=unprunenextchildat)
      this->unPruneNextChildNew();
    unprunemutex.unlock();
  }
}

void Tree::unPruneNow()
{
  float tmp=unprunenextchildat=this->unPruneMetric();
  //boost::mutex::scoped_lock lock(unprunemutex);
  unprunemutex.lock();
  if (tmp==unprunenextchildat)
    this->unPruneNextChildNew();
  unprunemutex.unlock();
}

float Tree::getUnPruneFactor(float *moveValues,float mean, int num) const
{
  float factor=1;
  if (params->uct_rave_unprune_decay>0)
  {
    factor=log((1000.0*gamma*params->uct_rave_unprune_decay)/(parent->raveplayouts+params->uct_rave_unprune_decay)+1);
    //ELO tests
    //
    // factor=gamma/parent->getChildrenTotalFeatureGamma()*exp(-parent->raveplayouts*params->uct_rave_unprune_decay);
    // -70ELO
    //factor=gamma/parent->getChildrenTotalFeatureGamma()*exp(-parent->playouts*params->uct_rave_unprune_decay);
    // -100ELO
    //factor=gamma/parent->getChildrenTotalFeatureGamma()/(parent->raveplayouts*params->uct_rave_unprune_decay+1);
    // -50ELO
    //factor=log(gamma+1.0)/(parent->raveplayouts*params->uct_rave_unprune_decay+1);
    // -20 ELO
    //factor=sqrt(gamma)/(parent->raveplayouts*params->uct_rave_unprune_decay+1);
    // -20ELO 
    //factor=sqrt(gamma)/(parent->playouts*params->uct_rave_unprune_decay+1);
    // -40ELO
    //factor=sqrt(gamma)/(parent->playouts*params->uct_rave_unprune_decay+10);
    // -30ELO
    //factor=pow(gamma,0.1)/(parent->raveplayouts*params->uct_rave_unprune_decay+1);
    // -20ELO
    //factor=pow(gamma,0.22)/(parent->raveplayouts*params->uct_rave_unprune_decay+1);
    // -15elo
    //factor=pow(gamma,0.5)/pow(parent->raveplayouts*params->uct_rave_unprune_decay+1,0.5);
    // -20ELO
    //factor=pow(gamma,params->test_p6)/(parent->raveplayouts*params->uct_rave_unprune_decay+1);
    
    //factor=pow(gamma,params->test_p6)*exp(-pow(parent->raveplayouts*params->uct_rave_unprune_decay,params->test_p7));
    //factor=pow(gamma,params->test_p6)*(params->test_p5+exp(-parent->raveplayouts/params->uct_rave_unprune_decay));
    //factor=pow(gamma,params->test_p6);
    
  }
  else
    factor=gamma/parent->getChildrenTotalFeatureGamma();
  //fprintf(stderr,"unprunefactore %f %f %f\n",gamma,parent->raveplayouts,factor);
  if (params->uct_criticality_unprune_factor>0 && (params->uct_criticality_siblings?parent->playouts:playouts)>(params->uct_criticality_min_playouts))
  {
    if (params->uct_criticality_unprune_multiply)
      factor*=(1+params->uct_criticality_unprune_factor*this->getCriticality());
    else
      factor+=params->uct_criticality_unprune_factor*this->getCriticality();
  }
  if (params->uct_prior_unprune_factor>0)
  {
    //fprintf(stderr,"prior wins? %f %f %f\n",wins,playouts,this->getRatio());
    factor*=wins*params->uct_prior_unprune_factor;
  }
  if (params->uct_rave_unprune_factor>0 && this->getRAVEPlayouts ()>1)
  {
    if (params->uct_rave_unprune_multiply)
      factor*=(1+params->uct_rave_unprune_factor*this->getRAVERatio());
    else
      factor+=params->uct_rave_unprune_factor*this->getRAVERatio();
  }


  factor+=params->uct_criticality_rave_unprune_factor*this->getRAVERatio()*this->getCriticality();

  float terrOwn=params->engine->getTerritoryMap()->getPositionOwner(move.getPosition());
  if (move.getColor()==Go::WHITE)
    terrOwn=-terrOwn;

  //tested version was ok with parameters
  //optimized_settings +='param uct_area_owner_factor_a 2.8\n'
  //optimized_settings +='param uct_area_owner_factor_b 0.1\n'
  //optimized_settings +='param uct_area_owner_factor_c 1.3\n'
  factor+=params->uct_area_owner_factor_a*exp(-pow(params->uct_area_owner_factor_c*(terrOwn-params->uct_area_owner_factor_b),2));

  //factor+=(params->uct_area_owner_factor_a+params->uct_area_owner_factor_b*terrOwn+params->uct_area_owner_factor_c*terrOwn*terrOwn)*exp(-pow(params->test_p1*terrOwn,2));
  float terrCovar=params->engine->getCorrelation(move.getPosition());
  if (terrCovar <0) terrCovar=0;
  factor+=params->test_p1*terrCovar;
  
  if (params->uct_earlyrave_unprune_factor>0 && this->getEARLYRAVEPlayouts ()>1)
  {
    if (params->uct_rave_unprune_multiply)
      factor*=1+params->uct_earlyrave_unprune_factor*this->getEARLYRAVERatio();
    else
      factor+=params->uct_earlyrave_unprune_factor*this->getEARLYRAVERatio();
  }
  if (params->uct_reprune_factor>0.0)
    factor*=1.0/(1.0+log(1+params->uct_reprune_factor*playouts));

  if (params->uct_oldmove_unprune_factor>0.0 || params->uct_oldmove_unprune_factor_b>0.0)
  {
    if (moveValues!=NULL)
    {
      //use values in the tree
      if (move.isNormal() && moveValues[move.getPosition ()]-mean>0)
        factor*=1+(moveValues[move.getPosition ()]-mean)*params->uct_oldmove_unprune_factor_b*pow(num,params->uct_oldmove_unprune_factor_c);
    }
    //else
    {
      factor*=1+params->engine->getOldMoveValue(move)*params->uct_oldmove_unprune_factor; //use the old values, which are not in the tree anymore 
    }
  }
  return factor;
}

void Tree::allowContinuedPlay()
{
  terminaloverride=true;
  hasTerminalWinrate=false;
}

Tree *Tree::getRobustChild(bool descend) const
{
  float bestsims=0;
  Tree *besttree=NULL;
  
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if (!(*iter)->isTerminalLose() && !(*iter)->isSuperkoViolation() && ((*iter)->getPlayouts()>bestsims || (*iter)->isTerminalWin() || besttree==NULL))
    {
      besttree=(*iter);
      bestsims=(*iter)->getPlayouts();
      if (besttree->isTerminalWin())
        break;
    }
  }
  
  if (besttree==NULL)
    return NULL;
  
  if (besttree->isLeaf() || !descend)
    return besttree;
  else
    return besttree->getRobustChild(descend);
}

Tree *Tree::getUrgentChild(Worker::Settings *settings)
{
  float besturgency=0;
  Tree *besttree=NULL;

  bool skiprave=false;
  if (params->rave_skip>0 && (params->rave_skip)>(settings->rand->getRandomReal()))
    skiprave=true;
  
  if (!superkoprunedchildren && playouts>(params->rules_superko_prune_after))
    this->pruneSuperkoViolations();
  
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if ((*iter)->isPrimary() && !(*iter)->isPruned())
    {
      float urgency;
      
      if ((*iter)->getPlayouts()==0 && (*iter)->getRAVEPlayouts()==0)
        urgency=(*iter)->getUrgency(skiprave)+(settings->rand->getRandomReal()/1000);
      else
      {
        urgency=(*iter)->getUrgency(skiprave);
        if (params->random_f>0)
          urgency-=params->random_f*log(settings->rand->getRandomReal());
        if (params->debug_on)
          fprintf(stderr,"[urg]:%s %.3f %.2f(%f) %.2f(%f)\n",(*iter)->getMove().toString(params->board_size).c_str(),urgency,(*iter)->getRatio(),(*iter)->getPlayouts(),(*iter)->getRAVERatio(),(*iter)->getRAVEPlayouts());
      }
          
      if (urgency>besturgency || besttree==NULL)
      {
        besttree=(*iter);
        besturgency=urgency;
      }
    }
  }
  
  if (besttree!=NULL && besttree->isTerminalLose() && this->hasPrunedChildren())
  {
    //fprintf(stderr,"ERROR! Unpruning didn't occur! (%d,%d,%d)\n",children->size(),prunedchildren,superkochildrenviolations);
    while (this->hasPrunedChildren() && !this->hasOneUnprunedChildNotTerminalLoss())
      this->unPruneNow(); //unprune a non-terminal loss
  }
  
  if (params->debug_on)
  {
    if (besttree!=NULL)
      fprintf(stderr,"[best]:%s (%d,%d)\n",besttree->getMove().toString(params->board_size).c_str(),(int)(children->size()-prunedchildren),superkochildrenviolations);
    else
      fprintf(stderr,"WARNING! No urgent move found\n");
  }
  
  if (besttree==NULL)
    return NULL;
  
  bool busyexpanding=false;
  if (params->move_policy==Parameters::MP_UCT && besttree->isLeaf() && !besttree->isTerminal())
  {
    if (besttree->getPlayouts()>params->uct_expand_after)
      busyexpanding=!besttree->expandLeaf(settings);
  }
  
  if (params->uct_virtual_loss)
    besttree->addVirtualLoss();
  
  if (besttree->isLeaf() || busyexpanding)
    return besttree;
  else
    return besttree->getUrgentChild(settings);
}

void Tree::presetRave (float ravew,float ravep)
{
  params->engine->addpresetplayout (ravep);
  ravewins+=params->uct_preset_rave_f*ravew; 
  raveplayouts+=params->uct_preset_rave_f*ravep;
  //fprintf(stderr,"ravewins %f raveplayouts %f\n",ravewins,raveplayouts);
}
void Tree::presetRaveEarly (float ravew,float ravep)
{
  earlyravewins+=params->uct_preset_rave_f*ravew; 
  earlyraveplayouts+=params->uct_preset_rave_f*ravep;
  fprintf(stderr,"ravewins %f raveplayouts %f\n",ravewins,raveplayouts);
}

bool Tree::expandLeaf(Worker::Settings *settings)
{
  float *rwins=NULL;
  float *rplayouts=NULL;

  //earlyrave preset does not seem to work, bug or just bad?? commented out!!!
  //float *earlyrwins=NULL;
  //float *earlyrplayouts=NULL;
  if (!this->isLeaf())
    return true;
  else if (this->isTerminal())
    fprintf(stderr,"WARNING! Trying to expand a terminal node!\n");
  
  //boost::mutex::scoped_try_lock lock(expandmutex);
  //expandmutex.lock();
  if (!expandmutex.try_lock())
    return false;

  //this is never executed?
  if (!this->isLeaf())
  {
    //fprintf(stderr,"Node was already expanded!\n");
    expandmutex.unlock();
    return true;
  }
  
  std::list<Go::Move> startmoves=this->getMovesFromRoot();
  Go::Board *startboard=params->engine->getCurrentBoard()->copy();
  
  //gtpe->getOutput()->printfDebug("[moves]:"); //!!!
  for(std::list<Go::Move>::iterator iter=startmoves.begin();iter!=startmoves.end();++iter)
  {
    //gtpe->getOutput()->printfDebug(" %s",(*iter).toString(boardsize).c_str()); //!!!
    startboard->makeMove((*iter));
    if (startboard->getPassesPlayed()>=2 || (*iter).isResign())
    {
      delete startboard;
      fprintf(stderr,"WARNING! Trying to expand a terminal node? (passes:%d)\n",startboard->getPassesPlayed());
      expandmutex.unlock();
      return true;
    }
  }
  //gtpe->getOutput()->printfDebug("\n"); //!!!
  
  Go::Color col=startboard->nextToMove();
  
  bool winnow=Go::Board::isWinForColor(col,startboard->score()-params->engine->getHandiKomi());
  Tree *nmt=new Tree(params,startboard->getZobristHash(params->engine->getZobristTable()),Go::Move(col,Go::Move::PASS));
  if (winnow)
    nmt->addPriorWins(1);
  #if UCT_PASS_DETER>0
    if (startboard->getPassesPlayed()==0 && !(params->surewin_expected && col==params->engine->getCurrentBoard()->nextToMove()))
      nmt->addPriorLoses(UCT_PASS_DETER);
  #endif
  nmt->addRAVEWins(params->rave_init_wins,false);
  nmt->addRAVEWins(params->rave_init_wins,true);
  this->addChild(nmt);

  // here we could add preknown rave values?!
  
  if (params->uct_preset_rave_f!=0.0 && this->getParent())
  {
    Tree *p=this->getParent();
    bool twohigher=false;
    if (p->getParent())
    {
      twohigher=true;
      p=p->getParent();
    }
    int posmax=startboard->getPositionMax();
    //fprintf(stderr,"posmax %d\n",posmax);
    rwins=new float[posmax];
    rplayouts=new float[posmax];
    //earlyrwins=new float[posmax];
    //earlyrplayouts=new float[posmax];
    for (int i=0;i<posmax;i++)
    {
      rplayouts[i]=-1;
      //earlyrplayouts[i]=-1;
    }
    std::list<Tree*> *childrenTmp=p->getChildren();
    for(std::list<Tree*>::iterator iter=childrenTmp->begin();iter!=childrenTmp->end();++iter) 
    {
      int pos=(*iter)->getMove().getPosition();
      if (pos>=0)
      {
        if (!twohigher)
        {
          rwins[pos]=(*iter)->getRAVEWinsOther();
          rplayouts[pos]=(*iter)->getRAVEPlayoutsOther();
        //earlyrwins[pos]=(*iter)->getRAVEWinsOtherEarly();
        //earlyrplayouts[pos]=(*iter)->getRAVEPlayoutsOtherEarly();
        }
        else
        {
          rwins[pos]=(*iter)->getRAVEWins();
          rplayouts[pos]=(*iter)->getRAVEPlayouts();
        //earlyrwins[pos]=(*iter)->getRAVEWinsEarly();
        //earlyrplayouts[pos]=(*iter)->getRAVEPlayoutsEarly();
        }
      }
    }
  }
  
  if (startboard->numOfValidMoves(col)>0)
  {
    Go::BitBoard *validmovesbitboard=startboard->getValidMoves(col);
    for (int p=0;p<startboard->getPositionMax();p++)
    {
      if (validmovesbitboard->get(p))
      {
        bool violation=false;
        Go::ZobristHash hash=0;
        if (params->rules_positional_superko_enabled && !params->rules_superko_top_ply && !params->rules_superko_at_playout)
        {
          Go::Board *thisboard=startboard->copy();
          thisboard->makeMove(Go::Move(col,p));
          hash=thisboard->getZobristHash(params->engine->getZobristTable());
          delete thisboard;
          
          violation=this->isSuperkoViolationWith(hash);
          
          if (!violation)
            violation=params->engine->getZobristHashTree()->hasHash(hash);
        }
        
        //if (violation)
        //  fprintf(stderr,"superko violation avoided\n");
        
        if (!violation)
        {
          Tree *nmt=new Tree(params,hash,Go::Move(col,p));
          
          if (params->uct_pattern_prior>0 && !startboard->weakEye(col,p))
          {
            unsigned int pattern=Pattern::ThreeByThree::makeHash(startboard,p);
            if (col==Go::WHITE)
              pattern=Pattern::ThreeByThree::invert(pattern);
            if (params->engine->getPatternTable()->isPattern(pattern))
              nmt->addPriorWins(params->uct_pattern_prior);
          }
          nmt->addRAVEWins(params->rave_init_wins,false);
          nmt->addRAVEWins(params->rave_init_wins,true);

          if (rplayouts && rplayouts[nmt->getMove().getPosition()]>0)
          {
            //fprintf(stderr,"%d %f %f\n",nmt->getMove().getPosition(),rwins[nmt->getMove().getPosition()],rplayouts[nmt->getMove().getPosition()]);
            nmt->presetRave(rwins[nmt->getMove().getPosition()],rplayouts[nmt->getMove().getPosition()]);
          }
          //if (earlyrplayouts && earlyrplayouts[nmt->getMove().getPosition()]>0)
          //{
            //fprintf(stderr,"%d %f %f\n",nmt->getMove().getPosition(),rwins[nmt->getMove().getPosition()],rplayouts[nmt->getMove().getPosition()]);
          //  nmt->presetRaveEarly(earlyrwins[nmt->getMove().getPosition()],earlyrplayouts[nmt->getMove().getPosition()]);
          //}
          this->addChild(nmt);
        }
      }
    }



    if (rwins) delete rwins;
    if (rplayouts) delete rplayouts;
    //if (earlyrwins) delete earlyrwins;
    //if (earlyrplayouts) delete earlyrplayouts;
    
    if (params->uct_playoutmove_prior>0)
    {
      //try unpruning a playout move?!
      Go::Move tmpmove;
      params->engine->getOnePlayoutMove(startboard, startboard->nextToMove(),&tmpmove);       
      fprintf(stderr,"test %s\n",tmpmove.toString (params->board_size).c_str());
      Tree *mt=this->getChild(tmpmove);
      if (mt!=NULL)
        mt->addPriorWins(params->uct_playoutmove_prior);
    }

    if (params->uct_atari_prior>0)
    {
      std::ourset<Go::Group*> *groups=startboard->getGroups();
      for(std::ourset<Go::Group*>::iterator iter=groups->begin();iter!=groups->end();++iter) 
      {
        if ((*iter)->inAtari())
        {
          int liberty=(*iter)->getAtariPosition();
          bool iscaptureorconnect=startboard->isCapture(Go::Move(col,liberty)) || startboard->isExtension(Go::Move(col,liberty));
          if (startboard->validMove(Go::Move(col,liberty)) && iscaptureorconnect)
          {
            Tree *mt=this->getChild(Go::Move(col,liberty));
            if (mt!=NULL)
              mt->addPriorWins(params->uct_atari_prior);
          }
        }
      }
    }
  }
  
  if (params->uct_symmetry_use)
  {
    Go::Board::Symmetry sym=startboard->getSymmetry();
    if (sym!=Go::Board::NONE)
    {
      for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
      {
        int pos=(*iter)->getMove().getPosition();
        Go::Board::SymmetryTransform trans=startboard->getSymmetryTransformToPrimary(sym,pos);
        int primarypos=startboard->doSymmetryTransform(trans,pos);
        if (pos!=primarypos)
        {
          Tree *pmt=this->getChild(Go::Move(col,primarypos));
          if (pmt!=NULL)
          {
            if (!pmt->isPrimary())
              fprintf(stderr,"WARNING! bad primary\n");
            (*iter)->setPrimary(pmt);
          }
          else
            fprintf(stderr,"WARNING! missing primary\n");
        }
      }
    }
  }
  
  if (params->uct_progressive_widening_enabled || params->uct_progressive_bias_enabled)
  {
    Go::ObjectBoard<int> *cfglastdist=NULL;
    Go::ObjectBoard<int> *cfgsecondlastdist=NULL;
    params->engine->getFeatures()->computeCFGDist(startboard,&cfglastdist,&cfgsecondlastdist);

    //int now_unpruned=this->getUnprunedNum();
    //fprintf(stderr,"debugging %d\n",now_unpruned);
    for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
    {
      if ((*iter)->isPrimary())
      {
        float gamma=params->engine->getFeatures()->getMoveGamma(startboard,cfglastdist,cfgsecondlastdist,(*iter)->getMove(),true,true);
        (*iter)->setFeatureGamma(gamma);
        //if ((*iter)->getMove().toString(params->board_size).compare("B:E1")==0)
        //  fprintf(stderr,"move %s %f\n",(*iter)->getMove().toString(params->board_size).c_str(),gamma);
      }
    }
    
    if (cfglastdist!=NULL)
      delete cfglastdist;
    if (cfgsecondlastdist!=NULL)
      delete cfgsecondlastdist;
    
    if (params->uct_progressive_widening_enabled)
    {
      this->pruneChildren();
      for (int i=0;i<params->uct_progressive_widening_init;i++)
      {
        this->unPruneNow(); //unprune a child
      }
      while (this->hasPrunedChildren() && !this->hasOneUnprunedChildNotTerminalLoss())
        this->unPruneNow(); //unprune a non-terminal loss
    }
  }
  
  beenexpanded=true;
  delete startboard;
  expandmutex.unlock();
  return true;
}

void Tree::updateRAVE(Go::Color wincol,Go::BitBoard *blacklist,Go::BitBoard *whitelist,bool early)
{
  if (params->rave_moves<=0)
    return;
  
  //fprintf(stderr,"[update rave] %s (%c)\n",move.toString(params->board_size).c_str(),Go::colorToChar(wincol));
  
  if (!this->isRoot())
  {
    parent->updateRAVE(wincol,blacklist,whitelist,early);

    if (this->getMove().isNormal())
    {
      int pos=this->getMove().getPosition();
      if (this->getMove().getColor()==Go::BLACK)
        blacklist->clear(pos);
      else
        whitelist->clear(pos);
    }
  }
  
  if (!this->isLeaf())
  {
    for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
    {
      if (!(*iter)->getMove().isPass() && !(*iter)->getMove().isResign())
      {
        Go::Color col=(*iter)->getMove().getColor();
        int pos=(*iter)->getMove().getPosition();
        
        if ((col==Go::BLACK?blacklist:whitelist)->get(pos))
        {
          if ((*iter)->isPrimary())
          {
            if (col==wincol)
              (*iter)->addRAVEWin(early);
            else
              (*iter)->addRAVELose(early);
          }
          else
          {
            Tree *primary=(*iter)->getPrimary();
            if (col==wincol)
              primary->addRAVEWin(early);
            else
              primary->addRAVELose(early);
          }
        }
        //check for the other color
        if ((col==Go::WHITE?blacklist:whitelist)->get(pos))
        {
          if ((*iter)->isPrimary())
          {
            if (Go::otherColor(col)==wincol)
              (*iter)->addRAVEWinOther(early);
            else
              (*iter)->addRAVELoseOther(early);
          }
          else
          {
            Tree *primary=(*iter)->getPrimary();
            if (Go::otherColor(col)==wincol)
              primary->addRAVEWinOther(early);
            else
              primary->addRAVELoseOther(early);
          }
        }
      }
    }
  }
}

float Tree::getProgressiveBias() const
{
  float bias=params->uct_progressive_bias_h*gamma;
  if (params->uct_progressive_bias_scaled>0.0 && !this->isRoot())
    bias/=parent->getChildrenTotalFeatureGamma();
  if (params->uct_progressive_bias_relative && !this->isRoot())
    bias/=parent->getMaxChildFeatureGamma();
  bias=pow(bias,params->uct_progressive_bias_exponent);
  //return (bias+biasbonus)*exp(-(float)playouts/params->uct_progressive_bias_moves); // /pow(playouts+1,params->uct_criticality_urgency_decay);
  //return (bias+biasbonus)/pow(playouts+1,params->test_p4);
  return (bias+biasbonus)/(playouts+1);
}

bool Tree::isSuperkoViolationWith(Go::ZobristHash h) const
{
  //fprintf(stderr,"0x%016llx 0x%016llx\n",h,hash);
  if (hash==h)
    return true;
  else if (!this->isRoot())
    return parent->isSuperkoViolationWith(h);
  else
    return false;
}

void Tree::setFeatureGamma(float g)
{
  gamma=g;
  if (!this->isRoot())
  {
    parent->childrentotalgamma+=gamma;
    if (gamma>(parent->maxchildgamma))
      parent->maxchildgamma=gamma;
  }
}

void Tree::pruneSuperkoViolations()
{
  if (!superkoprunedchildren && !this->isLeaf() && params->rules_positional_superko_enabled && params->rules_superko_top_ply && !params->rules_superko_at_playout)
  {
    std::list<Go::Move> startmoves=this->getMovesFromRoot();
    Go::Board *startboard=params->engine->getCurrentBoard()->copy();
    
    for(std::list<Go::Move>::iterator iter=startmoves.begin();iter!=startmoves.end();++iter)
    {
      startboard->makeMove((*iter));
    }
    
    for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
    {
      if ((*iter)->getMove().isNormal())
      {
        Go::Board *thisboard=startboard->copy();
        thisboard->makeMove((*iter)->getMove());
        Go::ZobristHash hash=thisboard->getZobristHash(params->engine->getZobristTable());
        (*iter)->setHash(hash);
        delete thisboard;
        (*iter)->doSuperkoCheck();
        
        /*bool violation=this->isSuperkoViolationWith(hash);
        
        if (!violation)
          violation=params->engine->getZobristHashTree()->hasHash(hash);
        
        if (violation)
        {
          std::list<Tree*>::iterator tmpiter=iter;
          Tree *tmptree=(*iter);
          --iter;
          children->erase(tmpiter);
          if (tmptree->isPruned())
            prunedchildren--;
          delete tmptree;
        }*/
      }
    }
    
    /*if (prunedchildren==children->size())
    {
      unprunenextchildat=0;
      lastunprune=0;
      unprunebase=0;
      this->unPruneNow(); //unprune at least one child
    }*/
    
    delete startboard;
    superkoprunedchildren=true;
  }
}

float Tree::bestChildRatioDiff() const
{
  Tree *bestchild=this->getRobustChild();
  
  if (bestchild==NULL)
    return -1;
  
  return (this->getRatio()+bestchild->getRatio()-1);
}

Tree *Tree::getSecondRobustChild(const Tree *firstchild) const
{
  if (firstchild==NULL)
    firstchild=this->getRobustChild();
  
  Tree *besttree=NULL;
  float bestsims=0;
  
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if ((((*iter)->getPlayouts()>bestsims || (*iter)->isTerminalWin()) && (*iter)!=firstchild) || besttree==NULL)
    {
      besttree=(*iter);
      bestsims=(*iter)->getPlayouts();
      if (besttree->isTerminalWin())
        break;
    }
  }
  
  return besttree;
}

float Tree::secondBestPlayouts() const
{
  if (this->isRoot())
    return 0;
  
  Tree *secondbest=parent->getSecondRobustChild(this);
  if (secondbest==NULL)
    return 0;
  else
    return secondbest->getPlayouts();
}

float Tree::secondBestPlayoutRatio() const
{
  if (this->isRoot())
    return -1;
  
  Tree *secondbest=parent->getSecondRobustChild(this);
  if (secondbest==NULL)
    return -1;
  else
    return (this->getPlayouts()/secondbest->getPlayouts());
}

Tree *Tree::getBestRatioChild(float playoutthreshold) const
{
  Tree *besttree=NULL;
  float bestratio=0;
  
  for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
  {
    if (((*iter)->getRatio()>bestratio && (*iter)->getPlayouts()>playoutthreshold) || (*iter)->isTerminalWin())
    {
      besttree=(*iter);
      bestratio=(*iter)->getRatio();
      if (besttree->isTerminalWin())
        break;
    }
  }
  
  return besttree;
}

void Tree::updateCriticality(Go::Board *board, Go::Color wincol)
{
  if (params->uct_criticality_unprune_factor==0 && params->uct_criticality_urgency_factor==0 && params->playout_poolrave_criticality==0)
    return;
  //fprintf(stderr,"[crit_up]: %d %d\n",this->isRoot(),params->uct_criticality_siblings);
  
  if (params->uct_criticality_siblings)
  {
    for(std::list<Tree*>::iterator iter=children->begin();iter!=children->end();++iter) 
    {
      if ((*iter)->getMove().isNormal())
      {
        int pos=(*iter)->getMove().getPosition();
        bool black=(board->getScoredOwner(pos)==Go::BLACK);
        bool white=(board->getScoredOwner(pos)==Go::WHITE);
        bool winner=(board->getScoredOwner(pos)==wincol);
        (*iter)->addCriticalityStats(winner,black,white);
      }
    }
  }
  
  if (this->isRoot())
    return;
  else
  {
    if (!(params->uct_criticality_siblings) && move.isNormal())
    {
      int pos=move.getPosition();
      bool black=(board->getScoredOwner(pos)==Go::BLACK);
      bool white=(board->getScoredOwner(pos)==Go::WHITE);
      bool winner=(board->getScoredOwner(pos)==wincol);
      this->addCriticalityStats(winner,black,white);
    }
    
    parent->updateCriticality(board,wincol);
  }
}

void Tree::addCriticalityStats(bool winner, bool black, bool white)
{
  //fprintf(stderr,"[crit_add]: %d %d %d\n",winner,black,white);
  
  if (winner)
    ownedwinner++;
  if (black)
    ownedblack++;
  if (white)
    ownedwhite++;
}

float Tree::getCriticality() const
{
  if (!move.isNormal() || (params->uct_criticality_siblings && this->isRoot()))
    return 0;
  else
  {
    int plts=(int)(params->uct_criticality_siblings?parent->playouts:playouts);
    if (plts==0)
      return 0;
    float ratio=(params->uct_criticality_siblings?1-parent->getRatio():this->getRatio());
    float crit;
    if (move.getColor()==Go::BLACK)
      crit=(float)ownedwinner/plts-(ratio*ownedblack/plts+(1-ratio)*ownedwhite/plts);
    else
      crit=(float)ownedwinner/plts-(ratio*ownedwhite/plts+(1-ratio)*ownedblack/plts);
    
    //fprintf(stderr,"[crit]: %.2f\n",crit);
    
    if (crit>=0)
      return crit;
    else
      return 0;
  }
}

float Tree::getTerritoryOwner() const
{
  if (!move.isNormal() || (params->uct_criticality_siblings && this->isRoot()))
    return 0;
  else
  {
    int plts=(int)(params->uct_criticality_siblings?parent->playouts:playouts);
    if (plts==0)
      return 0;
    return (float)(ownedblack-ownedwhite)/plts;
  }
}

void Tree::doSuperkoCheck()
{
  //boost::mutex::scoped_lock lock(superkomutex);
  //superkomutex.lock();
  if (!superkomutex.try_lock())
    return;
  if (superkochecked)
  {
    superkomutex.unlock();
    return;
  }
  superkoviolation=false;
  if (move.isNormal())
  {
    if (!this->isRoot())
      superkoviolation=parent->isSuperkoViolationWith(hash);
    if (!superkoviolation)
      superkoviolation=params->engine->getZobristHashTree()->hasHash(hash);
    if (superkoviolation)
    {
      //fprintf(stderr,"superko violation pruned (%s)\n",move.toString(params->board_size).c_str());
      pruned=true;
      if (!this->isRoot())
      {
        parent->superkochildrenviolations++;
        parent->prunedchildren++;
        if (parent->prunedchildren==parent->children->size())
        {
          parent->unprunenextchildat=0;
          parent->lastunprune=0;
          parent->unprunebase=0;
          parent->unPruneNow(); //unprune at least one child
          while (parent->hasPrunedChildren() && !parent->hasOneUnprunedChildNotTerminalLoss())
            parent->unPruneNow(); //unprune a non-terminal loss
        }
      }
    }
  }
  superkochecked=true;
  superkomutex.unlock();
}

void Tree::resetNode()
{
  playouts=0;
  wins=0;
  scoresum=0;
  scoresumsq=0;
  raveplayouts=0;
  earlyraveplayouts=0;
  ravewins=0;
  earlyravewins=0;
  raveplayoutsother=0;
  ravewinsother=0;
  earlyraveplayoutsother=0;
  earlyravewinsother=0;
  hasTerminalWinrate=false;
  hasTerminalWin=false;
  decayedwins=0;
  decayedplayouts=0;
  
  #ifdef HAVE_MPI
    this->resetMpiDiff();
  #endif
}

#ifdef HAVE_MPI

void Tree::resetMpiDiff()
{
  mpi_lastplayouts=playouts;
  mpi_lastwins=wins;
}

void Tree::addMpiDiff(float plts, float wns)
{
  playouts+=plts;
  wins+=wns;
  this->resetMpiDiff();
  if (!this->isRoot() && parent->isRoot())
    parent->addMpiDiff(plts,plts-wns);
}

void Tree::fetchMpiDiff(float &plts, float &wns)
{
  plts=playouts-mpi_lastplayouts;
  wns=wins-mpi_lastwins;
  this->resetMpiDiff();
}
#endif

