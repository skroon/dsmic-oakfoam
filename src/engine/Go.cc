#include "Go.h"

Go::BitBoard::BitBoard(int s)
{
  size=s;
  sizesq=s*s;
  sizedata=1+(s+1)*(s+2);
  data=new bool[sizedata];
  for (int i=0;i<sizedata;i++)
    data[i]=false;
}

Go::BitBoard::~BitBoard()
{
  delete[] data;
}

std::string Go::Move::toString(int boardsize)
{
  if (this->isPass())
    return "PASS";
  else if (this->isResign())
    return "RESIGN";
  else
  {
    std::ostringstream ss;
    if (color==Go::BLACK)
      ss<<"B";
    else if (color==Go::WHITE)
      ss<<"W";
    ss<<"("<<this->getX(boardsize)<<","<<this->getY(boardsize)<<")";
    return ss.str();
  }
}

Go::Group::Group(Go::Board *brd, int pos)
{
  board=brd;
  color=board->getColor(pos);
  position=pos;
  stonescount=1;
  parent=NULL;
  pseudoliberties=0;
  libpossum=0;
  libpossumsq=0;
}

void Go::Group::addTouchingEmpties()
{
  int size=board->getSize();
  foreach_adjacent(position,p,{
    if (board->getColor(p)==Go::EMPTY)
      this->addPseudoLiberty(p);
  });
}

Go::Board::Board(int s)
{
  size=s;
  sizesq=s*s;
  sizedata=1+(s+1)*(s+2);
  data=new Go::Vertex[sizedata];
  
  for (int p=0;p<sizedata;p++)
  {
    if (p<=(size) || p>=(sizedata-size-1) || (p%(size+1))==0)
      this->setColor(p,Go::OFFBOARD);
    else
      this->setColor(p,Go::EMPTY);
    this->setGroup(p,NULL);
  }
  
  movesmade=0;
  nexttomove=Go::BLACK;
  passesplayed=0;
  simpleko=-1;
  
  lastmove=Go::Move(Go::BLACK,Go::Move::PASS);
  secondlastmove=Go::Move(Go::WHITE,Go::Move::PASS);
  
  blackvalidmoves=new Go::BitBoard(size);
  whitevalidmoves=new Go::BitBoard(size);
  this->refreshValidMoves();
  
  symmetryupdated=true;
  currentsymmetry=Go::Board::FULL;
}

Go::Board::~Board()
{
  delete blackvalidmoves;
  delete whitevalidmoves;
  
  //XXX: memory will get freed when pool is destroyed
  /*for(std::list<Go::Group*,Go::allocator_groupptr>::iterator iter=groups.begin();iter!=groups.end();++iter) 
  {
    pool_group.destroy((*iter));
  }*/
  groups.resize(0);
  
  delete[] data;
}

Go::Board *Go::Board::copy()
{
  Go::Board *copyboard;
  copyboard=new Go::Board(size);
  
  this->copyOver(copyboard);
  
  return copyboard;
}

void Go::Board::copyOver(Go::Board *copyboard)
{
  if (size!=copyboard->getSize())
    throw Go::Exception("cannot copy to a different size board");
  
  for (int p=0;p<sizedata;p++)
  {
    copyboard->data[p]=this->data[p];
  }
  
  copyboard->movesmade=this->movesmade;
  copyboard->nexttomove=this->nexttomove;
  copyboard->passesplayed=this->passesplayed;
  copyboard->simpleko=this->simpleko;
  
  copyboard->lastmove=this->lastmove;
  copyboard->secondlastmove=this->secondlastmove;
  
  copyboard->refreshGroups();
  
  if (copyboard->symmetryupdated)
      copyboard->updateSymmetry();
}

std::string Go::Board::toString()
{
  std::ostringstream ss;
  
  for (int y=size-1;y>=0;y--)
  {
    for (int x=0;x<size;x++)
    {
      Go::Color col=this->getColor(Go::Position::xy2pos(x,y,size));
      if (col==Go::BLACK)
        ss<<"X ";
      else if (col==Go::WHITE)
        ss<<"O ";
      else
        ss<<". ";
    }
    ss<<"\n";
  }
  
  return ss.str();
}

std::string Go::Board::toSGFString()
{
  std::ostringstream ss,ssb,ssw;
  
  for (int p=0;p<sizedata;p++)
  {
    if (this->getColor(p)==Go::BLACK)
    {
      ssb<<"[";
      ssb<<(char)(Go::Position::pos2x(p,size)+'a');
      ssb<<(char)(size-Go::Position::pos2y(p,size)+'a'-1);
      ssb<<"]";
    }
  }
  if (ssb.str().length()>0)
    ss<<"AB"<<ssb.str();
  for (int p=0;p<sizedata;p++)
  {
    if (this->getColor(p)==Go::WHITE)
    {
      ssw<<"[";
      ssw<<(char)(Go::Position::pos2x(p,size)+'a');
      ssw<<(char)(size-Go::Position::pos2y(p,size)+'a'-1);
      ssw<<"]";
    }
  }
  if (ssw.str().length()>0)
    ss<<"AW"<<ssw.str();
  ss<<"PL["<<Go::colorToChar(this->nextToMove())<<"]";
  return ss.str();
}

int Go::Board::score()
{
  Go::Board::ScoreVertex *scoredata;
  
  scoredata=new Go::Board::ScoreVertex[sizedata];
  for (int p=0;p<sizedata;p++)
  {
    if (this->getColor(p)==Go::OFFBOARD)
    {
      scoredata[p].touched=true;
      scoredata[p].color=Go::EMPTY;
    }
    else
    {
      scoredata[p].touched=false;
      scoredata[p].color=Go::EMPTY;
    }
  }
  
  for (int p=0;p<sizedata;p++)
  {
    if (this->getColor(p)!=Go::OFFBOARD && !scoredata[p].touched && this->getColor(p)!=Go::EMPTY)
    {
      this->spreadScore(scoredata,p,this->getColor(p));
    }
  }
  
  int s=0;
  
  for (int p=0;p<sizedata;p++)
  {
    Go::Color col=scoredata[p].color;
    if (col==Go::BLACK)
      s++;
    else if (col==Go::WHITE)
      s--;
  }
  
  delete[] scoredata;
  return s;
}

void Go::Board::spreadScore(Go::Board::ScoreVertex *scoredata, int pos, Go::Color col)
{
  bool wastouched=scoredata[pos].touched;
  
  if (this->getColor(pos)==Go::OFFBOARD || col==Go::OFFBOARD)
    return;
  
  if (this->getColor(pos)!=Go::EMPTY && col==Go::EMPTY)
    return;
  
  if (this->getColor(pos)!=Go::EMPTY)
  {
    scoredata[pos].touched=true;
    scoredata[pos].color=this->getColor(pos);
    if (!wastouched)
    {
      foreach_adjacent(pos,p,{
        this->spreadScore(scoredata,p,this->getColor(pos));
      });
    }
    return;
  }
  
  if (scoredata[pos].touched && scoredata[pos].color==Go::EMPTY && col==Go::EMPTY)
    return;
  
  if (scoredata[pos].touched && scoredata[pos].color!=col)
    col=Go::EMPTY;
  
  if (scoredata[pos].touched && scoredata[pos].color==col)
    return;
  
  scoredata[pos].touched=true;
  scoredata[pos].color=col;
  
  foreach_adjacent(pos,p,{
    this->spreadScore(scoredata,p,col);
  });
}

bool Go::Board::validMove(Go::Move move)
{
  Go::BitBoard *validmoves=(move.getColor()==Go::BLACK?blackvalidmoves:whitevalidmoves);
  return move.isPass() || move.isResign() || validmoves->get(move.getPosition());
}

bool Go::Board::validMoveCheck(Go::Move move)
{
  if (move.isPass() || move.isResign())
    return true;
  
  if (this->getColor(move.getPosition())!=Go::EMPTY)
    return false;
  else if (touchingAtLeastOneEmpty(move.getPosition()))
    return true;
  else
  {
    int pos=move.getPosition();
    Go::Color col=move.getColor();
    Go::Color othercol=Go::otherColor(col);
    bool isvalid=false;
    int captures=0;
    
    foreach_adjacent(pos,p,{
      if (this->getColor(p)==col || this->getColor(p)==othercol)
        this->getGroup(p)->removePseudoLiberty(pos);
    });
    
    foreach_adjacent(pos,p,{
      if (this->getColor(p)==col && this->getPseudoLiberties(p)>0)
        isvalid=true;
      else if (this->getColor(p)==othercol && this->getPseudoLiberties(p)==0)
      {
        captures+=this->getGroupSize(p);
        if (captures>1)
          isvalid=true;
      }
    });
    
    foreach_adjacent(pos,p,{
      if (this->getColor(p)==col || this->getColor(p)==othercol)
        this->getGroup(p)->addPseudoLiberty(pos);
    });
    
    if (isvalid)
      return true;
    
    if (captures==1)
    {
      if (pos==simpleko)
        return false;
      else
        return true;
    }
    
    return false;
  }
}

void Go::Board::makeMove(Go::Move move)
{
  if (nexttomove!=move.getColor())
  {
    fprintf(stderr,"WARNING! unexpected move color\n");
  }
  
  if (simpleko!=-1)
  {
    int kopos=simpleko;
    simpleko=-1;
    if (this->validMoveCheck(Go::Move(move.getColor(),kopos)))
      this->addValidMove(Go::Move(move.getColor(),kopos));
  }
  
  if (move.isPass() || move.isResign())
  {
    if (move.isPass())
      passesplayed++;
    nexttomove=Go::otherColor(move.getColor());
    movesmade++;
    secondlastmove=lastmove;
    lastmove=move;
    if (symmetryupdated)
      updateSymmetry();
    return;
  }
  
  if (!this->validMove(move))
  {
    fprintf(stderr,"invalid move at %d,%d\n",move.getX(size),move.getY(size));
    throw Go::Exception("invalid move");
  }
  
  movesmade++;
  nexttomove=Go::otherColor(nexttomove);
  passesplayed=0;
  secondlastmove=lastmove;
  lastmove=move;
  
  Go::Color col=move.getColor();
  Go::Color othercol=Go::otherColor(col);
  int pos=move.getPosition();
  int posko=-1;
  
  this->setColor(pos,col);
  
  Go::Group *thisgroup=pool_group.construct(this,pos);
  this->setGroup(pos,thisgroup);
  groups.push_back(thisgroup);
  
  thisgroup->addTouchingEmpties();
  
  foreach_adjacent(pos,p,{
    if (this->getColor(p)==col)
    {
      Go::Group *othergroup=this->getGroup(p);
      othergroup->removePseudoLiberty(pos);
      
      if (othergroup->numOfStones()>thisgroup->numOfStones())
        this->mergeGroups(othergroup,thisgroup);
      else
        this->mergeGroups(thisgroup,othergroup);
      thisgroup=thisgroup->find();
    }
    else if (this->getColor(p)==othercol)
    {
      Go::Group *othergroup=this->getGroup(p);
      othergroup->removePseudoLiberty(pos);
      
      if (othergroup->numOfPseudoLiberties()==0)
      {
        if (removeGroup(othergroup)==1)
        {
          if (posko==-1)
            posko=p;
          else
            posko=-2;
        }
      }
      else if (othergroup->numOfPseudoLiberties()<=4)
      {
        if (othergroup->inAtari())
        {
          int liberty=othergroup->getAtariPosition();
          this->addValidMove(Go::Move(col,liberty));
          if (this->touchingEmpty(liberty)==0 && !this->validMoveCheck(Go::Move(othercol,liberty)))
            this->removeValidMove(Go::Move(othercol,liberty));
        }
      }
    }
  });
  
  this->removeValidMove(Go::Move(Go::BLACK,pos));
  this->removeValidMove(Go::Move(Go::WHITE,pos));
  
  if (thisgroup->inAtari())
  {
    int liberty=thisgroup->getAtariPosition();
    if (this->touchingEmpty(liberty)==0 && !this->validMoveCheck(Go::Move(col,liberty)))
      this->removeValidMove(Go::Move(col,liberty));
    if (this->validMoveCheck(Go::Move(othercol,liberty)))
      this->addValidMove(Go::Move(othercol,liberty));
  }
  
  foreach_adjacent(pos,p,{
    if (this->getColor(p)==Go::EMPTY)
    {
      if (!this->validMoveCheck(Go::Move(othercol,p)))
        this->removeValidMove(Go::Move(othercol,p));
    }
  });
  
  if (posko>=0 && thisgroup->inAtari())
  {
    simpleko=posko;
    if (!this->validMoveCheck(Go::Move(othercol,simpleko)))
      this->removeValidMove(Go::Move(othercol,simpleko));
  }
  
  if (symmetryupdated)
    updateSymmetry();
}

void Go::Board::refreshValidMoves()
{
  this->refreshValidMoves(Go::BLACK);
  this->refreshValidMoves(Go::WHITE);
}

void Go::Board::refreshValidMoves(Go::Color col)
{
  (col==Go::BLACK?blackvalidmovecount:whitevalidmovecount)=0;
  Go::BitBoard *validmoves=(col==Go::BLACK?blackvalidmoves:whitevalidmoves);
  
  for (int p=0;p<sizedata;p++)
  {
    validmoves->clear(p);
    if (this->validMoveCheck(Go::Move(col,p)))
      this->addValidMove(Go::Move(col,p));
  }
}

void Go::Board::addValidMove(Go::Move move)
{
  Go::BitBoard *validmoves=(move.getColor()==Go::BLACK?blackvalidmoves:whitevalidmoves);
  int *validmovecount=(move.getColor()==Go::BLACK?&blackvalidmovecount:&whitevalidmovecount);
  
  if (!validmoves->get(move.getPosition()))
  {
    validmoves->set(move.getPosition());
    (*validmovecount)++;
  }
}

void Go::Board::removeValidMove(Go::Move move)
{
  Go::BitBoard *validmoves=(move.getColor()==Go::BLACK?blackvalidmoves:whitevalidmoves);
  int *validmovecount=(move.getColor()==Go::BLACK?&blackvalidmovecount:&whitevalidmovecount);
  
  if (validmoves->get(move.getPosition()))
  {
    validmoves->clear(move.getPosition());
    (*validmovecount)--;
  }
}

int Go::Board::touchingEmpty(int pos)
{
  int lib=0;
  
  foreach_adjacent(pos,p,{
    if (this->getColor(p)==Go::EMPTY)
      lib++;
  });
  
  return lib;
}

bool Go::Board::touchingAtLeastOneEmpty(int pos)
{
  foreach_adjacent(pos,p,{
    if (this->getColor(p)==Go::EMPTY)
      return true;
  });
  
  return false;
}

void Go::Board::refreshGroups()
{
  for (int p=0;p<sizedata;p++)
  {
    this->setGroup(p,NULL);
  }
  
  //XXX: memory will get freed when pool is destroyed
  //for(std::list<Go::Group*,Go::allocator_groupptr>::iterator iter=groups.begin();iter!=groups.end();++iter) 
  //{
  //  pool_group.destroy((*iter));
  //}
  groups.resize(0);
  
  for (int p=0;p<sizedata;p++)
  {
    if (this->getColor(p)!=Go::EMPTY && this->getColor(p)!=Go::OFFBOARD && this->getGroupWithoutFind(p)==NULL)
    {
      Go::Group *newgroup=pool_group.construct(this,p);
      this->setGroup(p,newgroup);
      newgroup->addTouchingEmpties();
    
      foreach_adjacent(p,q,{
        this->spreadGroup(q,newgroup);
      });
      groups.push_back(newgroup);
    }
  }
  
  this->refreshValidMoves();
}

void Go::Board::spreadGroup(int pos, Go::Group *group)
{
  if (this->getColor(pos)==group->getColor() && this->getGroupWithoutFind(pos)==NULL)
  {
    Go::Group *thisgroup=pool_group.construct(this,pos);
    this->setGroup(pos,thisgroup);
    groups.push_back(thisgroup);
    thisgroup->addTouchingEmpties();
    this->mergeGroups(group,thisgroup);
    
    foreach_adjacent(pos,p,{
      this->spreadGroup(p,group);
    });
  }
}

int Go::Board::removeGroup(Go::Group *group)
{
  group=group->find();
  int pos=group->getPosition();
  int s=group->numOfStones();
  Go::Color groupcol=group->getColor();
  
  groups.remove(group);
  
  std::list<int,Go::allocator_int> *possiblesuicides = new std::list<int,Go::allocator_int>();
  
  this->spreadRemoveStones(groupcol,pos,possiblesuicides);
  
  for(std::list<int,Go::allocator_int>::iterator iter=possiblesuicides->begin();iter!=possiblesuicides->end();++iter)
  {
    if (!this->validMoveCheck(Go::Move(groupcol,(*iter)))) 
      this->removeValidMove(Go::Move(groupcol,(*iter)));
  }
  
  possiblesuicides->resize(0);
  delete possiblesuicides;
  
  return s;
}

void Go::Board::spreadRemoveStones(Go::Color col, int pos, std::list<int,Go::allocator_int> *possiblesuicides)
{
  Go::Color othercol=Go::otherColor(col);
  //Go::Group *group=this->getGroupWithoutFind(pos); //see destroy() below
  
  this->setColor(pos,Go::EMPTY);
  this->setGroup(pos,NULL);
  
  foreach_adjacent(pos,p,{
    if (this->getColor(p)==col)
      this->spreadRemoveStones(col,p,possiblesuicides);
    else if (this->getColor(p)==othercol)
    {
      Go::Group *othergroup=this->getGroup(p);
      if (othergroup->inAtari())
      {
        int liberty=othergroup->getAtariPosition();
        this->addValidMove(Go::Move(othercol,liberty));
        possiblesuicides->push_back(liberty);
      }
      othergroup->addPseudoLiberty(pos);
    }
  });
  
  this->addValidMove(Go::Move(othercol,pos));
  this->addValidMove(Go::Move(col,pos));
  
  //XXX: memory will get freed when pool is destroyed
  //pool_group.destroy(group);
}

void Go::Board::mergeGroups(Go::Group *first, Go::Group *second)
{
  if (first==second)
    return;
  
  groups.remove(second);
  first->unionWith(second);
}

bool Go::Board::weakEye(Go::Color col, int pos)
{
  if (col==Go::EMPTY || col==Go::OFFBOARD || this->getColor(pos)!=Go::EMPTY)
    return false;
  else
  {
    foreach_adjacent(pos,p,{
      if (this->getColor(p)!=Go::OFFBOARD && (this->getColor(p)!=col || this->getGroup(p)->inAtari()))
        return false;
    });
    
    return true;
  }
}

bool Go::Board::isWinForColor(Go::Color col, float score)
{
  float k=0;
  
  if (col==Go::BLACK)
    k=1;
  else if (col==Go::WHITE)
    k=-1;
  else
    return false;
  
  return ((score*k)>0);
}

bool Go::Board::hasSymmetryVertical()
{
  if (simpleko!=-1)
    return false;
  
  int xlimit=size/2+size%2-1;
  for (int x=0;x<xlimit;x++)
  {
    for (int y=0;y<size;y++)
    {
      int pos1=Go::Position::xy2pos(x,y,size);
      int pos2=this->doSymmetryTransformPrimitive(Go::Board::VERTICAL,pos1);
      if (this->getColor(pos1)!=this->getColor(pos2))
      {
        //fprintf(stderr,"mismatch at %d,%d (%d)\n",x,y,xlimit);
        return false;
      }
    }
  }
  
  return true;
}

bool Go::Board::hasSymmetryHorizontal()
{
  if (simpleko!=-1)
    return false;
  
  int ylimit=size/2+size%2-1;
  for (int x=0;x<size;x++)
  {
    for (int y=0;y<ylimit;y++)
    {
      int pos1=Go::Position::xy2pos(x,y,size);
      int pos2=this->doSymmetryTransformPrimitive(Go::Board::HORIZONTAL,pos1);
      if (this->getColor(pos1)!=this->getColor(pos2))
      {
        //fprintf(stderr,"mismatch at %d,%d (%d)\n",x,y,ylimit);
        return false;
      }
    }
  }
  
  return true;
}

bool Go::Board::hasSymmetryDiagonalDown()
{
  if (simpleko!=-1)
    return false;
  
  for (int x=0;x<size;x++)
  {
    for (int y=0;y<=(size-x-1);y++)
    {
      //fprintf(stderr,"pos: %d,%d %d,%d\n",x,y,size-y-1,size-x-1);
      int pos1=Go::Position::xy2pos(x,y,size);
      int pos2=this->doSymmetryTransformPrimitive(Go::Board::DIAGONAL_DOWN,pos1);
      if (this->getColor(pos1)!=this->getColor(pos2))
      {
        //fprintf(stderr,"mismatch at %d,%d %d,%d\n",x,y,size-y-1,size-x-1);
        return false;
      }
    }
  }
  
  return true;
}

bool Go::Board::hasSymmetryDiagonalUp()
{
  if (simpleko!=-1)
    return false;
  
  for (int x=0;x<size;x++)
  {
    for (int y=0;y<=x;y++)
    {
      //fprintf(stderr,"pos: %d,%d %d,%d\n",x,y,y,x);
      int pos1=Go::Position::xy2pos(x,y,size);
      int pos2=this->doSymmetryTransformPrimitive(Go::Board::DIAGONAL_UP,pos1);
      if (this->getColor(pos1)!=this->getColor(pos2))
      {
        //fprintf(stderr,"mismatch at %d,%d %d,%d\n",x,y,y,x);
        return false;
      }
    }
  }
  
  return true;
}

Go::Board::Symmetry Go::Board::computeSymmetry()
{
  bool symvert=this->hasSymmetryVertical();
  bool symdiagup=this->hasSymmetryDiagonalUp();
  if (symvert && symdiagup)
    return Go::Board::FULL;
  else if (symvert && this->hasSymmetryHorizontal())
    return Go::Board::VERTICAL_HORIZONTAL;
  else if (symvert)
    return Go::Board::VERTICAL;
  else if (symdiagup && this->hasSymmetryDiagonalDown())
    return Go::Board::DIAGONAL_BOTH;
  else if (symdiagup)
    return Go::Board::DIAGONAL_UP;
  else if (this->hasSymmetryHorizontal())
    return Go::Board::HORIZONTAL;
  else if (this->hasSymmetryDiagonalDown())
    return Go::Board::DIAGONAL_DOWN;
  else
    return Go::Board::NONE;
}

std::string Go::Board::getSymmetryString(Go::Board::Symmetry sym)
{
  if (sym==Go::Board::FULL)
    return "FULL";
  else if (sym==Go::Board::VERTICAL_HORIZONTAL)
    return "VERTICAL_HORIZONTAL";
  else if (sym==Go::Board::VERTICAL)
    return "VERTICAL";
  else if (sym==Go::Board::HORIZONTAL)
    return "HORIZONTAL";
  else if (sym==Go::Board::DIAGONAL_BOTH)
    return "DIAGONAL_BOTH";
  else if (sym==Go::Board::DIAGONAL_UP)
    return "DIAGONAL_UP";
  else if (sym==Go::Board::DIAGONAL_DOWN)
    return "DIAGONAL_DOWN";
  else
    return "NONE";
}

void Go::Board::updateSymmetry()
{
  #if SYMMETRY_ONLYDEGRAGE
    if (currentsymmetry==Go::Board::NONE)
      return;
    else
      currentsymmetry=this->computeSymmetry(); //can be optimized
  #else
    currentsymmetry=this->computeSymmetry(); //can be optimized (not used)
  #endif
}

int Go::Board::doSymmetryTransformPrimitive(Go::Board::Symmetry sym, int pos)
{
  int x=Go::Position::pos2x(pos,size);
  int y=Go::Position::pos2y(pos,size);
  if (sym==Go::Board::VERTICAL)
    return Go::Position::xy2pos(size-x-1,y,size);
  else if (sym==Go::Board::HORIZONTAL)
    return Go::Position::xy2pos(x,size-y-1,size);
  else if (sym==Go::Board::DIAGONAL_UP)
    return Go::Position::xy2pos(y,x,size);
  else if (sym==Go::Board::DIAGONAL_DOWN)
    return Go::Position::xy2pos(size-y-1,size-x-1,size);
  else
    return -1;
}

int Go::Board::doSymmetryTransformToPrimary(Go::Board::Symmetry sym, int pos)
{
  return doSymmetryTransform(getSymmetryTransformToPrimary(sym,pos),pos);
}

Go::Board::SymmetryTransform Go::Board::getSymmetryTransformToPrimary(Go::Board::Symmetry sym, int pos)
{
  int x=Go::Position::pos2x(pos,size);
  int y=Go::Position::pos2y(pos,size);
  Go::Board::SymmetryTransform trans={false,false,false};
  
  if (sym==Go::Board::VERTICAL)
  {
    if (x>(size-x-1))
      trans.invertX=true;
  }
  else if (sym==Go::Board::HORIZONTAL)
  {
    if (y>(size-y-1))
      trans.invertY=true;
  }
  else if (sym==Go::Board::VERTICAL_HORIZONTAL)
  {
    if (x>(size-x-1))
      trans.invertX=true;
    if (y>(size-y-1))
      trans.invertY=true;
  }
  else if (sym==Go::Board::DIAGONAL_UP)
  {
    if (x>y)
      trans.swapXY=true;
  }
  else if (sym==Go::Board::DIAGONAL_DOWN)
  {
    if ((x+y)>((size-y-1)+(size-x-1)))
    {
      trans.invertX=true;
      trans.invertY=true;
      trans.swapXY=true;
    }
  }
  else if (sym==Go::Board::DIAGONAL_BOTH)
  {
    int nx=x;
    int ny=y;
    if ((x+y)>((size-y-1)+(size-x-1)))
    {
      trans.invertX=true;
      trans.invertY=true;
      trans.swapXY=true;
      nx=(size-y-1);
      ny=(size-x-1);
    }
    
    if (nx>ny)
      trans.swapXY=!trans.swapXY;
  }
  else if (sym==Go::Board::FULL)
  {
    int nx=x;
    int ny=y;
    
    if (x>(size-x-1))
    {
      trans.invertX=true;
      nx=(size-x-1);
    }
    
    if (y>(size-y-1))
    {
      trans.invertY=true;
      ny=(size-y-1);
    }
    
    if (nx>ny)
      trans.swapXY=true;
  }
  
  return trans;
}

int Go::Board::doSymmetryTransform(Go::Board::SymmetryTransform trans, int pos, bool reverse)
{
  if (reverse)
    return Go::Board::doSymmetryTransformStaticReverse(trans,size,pos);
  else
    return Go::Board::doSymmetryTransformStatic(trans,size,pos);
}

int Go::Board::doSymmetryTransformStatic(Go::Board::SymmetryTransform trans, int size, int pos)
{
  int x=Go::Position::pos2x(pos,size);
  int y=Go::Position::pos2y(pos,size);
  
  if (trans.invertX)
    x=(size-x-1);
  
  if (trans.invertY)
    y=(size-y-1);
  
  if (trans.swapXY)
  {
    int t=x;
    x=y;
    y=t;
  }
  
  return Go::Position::xy2pos(x,y,size);
}

int Go::Board::doSymmetryTransformStaticReverse(Go::Board::SymmetryTransform trans, int size, int pos)
{
  int x=Go::Position::pos2x(pos,size);
  int y=Go::Position::pos2y(pos,size);
  
  if (trans.swapXY)
  {
    int t=x;
    x=y;
    y=t;
  }
  
  if (trans.invertX)
    x=(size-x-1);
  
  if (trans.invertY)
    y=(size-y-1);
  
  return Go::Position::xy2pos(x,y,size);
}



