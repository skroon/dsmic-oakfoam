#include "Engine.h"

Engine::Engine(Gtp::Engine *ge)
{
  gtpe=ge;
  
  std::srand(std::time(0));
  
  boardsize=9;
  currentboard=new Go::Board(boardsize);
  komi=5.5;
  
  this->addGtpCommands();
}

Engine::~Engine()
{
  delete currentboard;
}

void Engine::addGtpCommands()
{
  gtpe->addFunctionCommand("boardsize",this,&Engine::gtpBoardSize);
  gtpe->addFunctionCommand("clear_board",this,&Engine::gtpClearBoard);
  gtpe->addFunctionCommand("komi",this,&Engine::gtpKomi);
  gtpe->addFunctionCommand("play",this,&Engine::gtpPlay);
  gtpe->addFunctionCommand("genmove",this,&Engine::gtpGenMove);
  gtpe->addFunctionCommand("showboard",this,&Engine::gtpShowBoard);
}

void Engine::gtpBoardSize(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("size is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  me->setBoardSize(cmd->getIntArg(0)); //TODO:: check for valid size
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpClearBoard(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  me->clearBoard();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpKomi(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("komi is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  me->setKomi(cmd->getFloatArg(0));
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpPlay(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=2)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("move is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Color gtpcol = cmd->getColorArg(0);
  Gtp::Vertex vert = cmd->getVertexArg(1);
  
  if (gtpcol==Gtp::INVALID || (vert.x==-3 && vert.y==-3))
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid move");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Go::Move move=Go::Move((gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE),vert.x,vert.y);
  if (!me->isMoveAllowed(move))
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("illegal move");
    gtpe->getOutput()->endResponse();
    return;
  }
  me->makeMove(move);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpGenMove(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("color is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Color gtpcol = cmd->getColorArg(0);
  
  if (gtpcol==Gtp::INVALID)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid color");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Go::Move *move;
  me->generateMove((gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE),&move);
  
  Gtp::Vertex vert= {move->getX(),move->getY()};
  delete move;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printVertex(vert);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpShowBoard(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("current board:\n");
  me->currentboard->print();
  gtpe->getOutput()->endResponse(true);
}

void Engine::generateMove(Go::Color col, Go::Move **move)
{
  //*move=new Go::Move(col,Go::Move::PASS);
  //this->randomValidMove(currentboard,col,move);
  //this->makeMove(**move);
  
  /*
  playoutboard=currentboard->copy();
  randomPlayout(playoutboard,col);
  playoutboard->print();
  printf("scoreable: %d\n",playoutboard->scoreable());
  delete playoutboard;
  */
  
  Util::MoveTree *movetree;
  Go::Board *playoutboard;
  Go::Move playoutmove;
  
  playoutmove=Go::Move();//Go::Move(col,Go:Move::PASS);
  movetree= new Util::MoveTree(playoutmove);
  movetree->addLose();
  
  for (int x=0;x<boardsize;x++)
  {
    for (int y=0;y<boardsize;y++)
    {
      playoutmove=Go::Move(col,x,y);
      if (currentboard->validMove(playoutmove))
      {
        Util::MoveTree *nmt=new Util::MoveTree(playoutmove);
        
        for (int i=0;i<PLAYOUTS_PER_MOVE;i++)
        {
          playoutboard=currentboard->copy();
          playoutboard->makeMove(playoutmove);
          randomPlayout(playoutboard,Go::otherColor(col));
          if (Util::isWinForColor(col,playoutboard->score()-komi))
            nmt->addWin();
          else
            nmt->addLose();
          delete playoutboard;
        }
        
        movetree->addSibling(nmt);
      }
    }
  }
  
  float bestratio=0;
  Go::Move bestmove;//=new Go::Move(col,Go:Move::PASS);
  bestratio=0;
  //bestmove=new Go::Move(col,Go:Move::PASS);
  
  Util::MoveTree *cmt=movetree;
  
  while (cmt!=NULL)
  {
    if (cmt->getPlayouts()>0)
    {
      float r=(float)cmt->getWins()/cmt->getPlayouts();
      //printf("ratio for (%d,%d) %f\n",cmt->getMove().getX(),cmt->getMove().getY(),r);
      if (r>bestratio)
      {
        bestratio=r;
        bestmove=cmt->getMove();
      }
    }
    cmt=cmt->getSibling();
  }
  
  //bestmove.print();
  
  if (bestratio<RESIGN_THRESHOLD)
    *move=new Go::Move(col,Go::Move::RESIGN);
  else
    *move=new Go::Move(col,bestmove.getX(),bestmove.getY());
  
  this->makeMove(**move);
  
  delete movetree;
}

bool Engine::isMoveAllowed(Go::Move move)
{
  return currentboard->validMove(move);
}

void Engine::makeMove(Go::Move move)
{
  currentboard->makeMove(move);
}

int Engine::getBoardSize()
{
  return currentboard->getSize();
}

void Engine::setBoardSize(int s)
{
  delete currentboard;
  currentboard = new Go::Board(s);
}

void Engine::clearBoard()
{
  int size=currentboard->getSize();
  delete currentboard;
  currentboard = new Go::Board(size);
}

Go::Board *Engine::getCurrentBoard()
{
  return currentboard;
}

float Engine::getKomi()
{
  return komi;
}

void Engine::setKomi(float k)
{
  komi=k;
}

void Engine::randomValidMove(Go::Board *board, Go::Color col, Go::Move **move)
{
  int i,x,y;
  
  i=0;
  do
  {
    x=(int)(std::rand()/((double)RAND_MAX+1)*boardsize);
    y=(int)(std::rand()/((double)RAND_MAX+1)*boardsize);
    i++;
    if (i>(boardsize*boardsize*2))
    {
      *move=new Go::Move(col,Go::Move::PASS);
      return;
    }
  } while (!board->validMove(Go::Move(col,x,y)) || board->weakEye(col,x,y));
  
  *move=new Go::Move(col,x,y);
}

void Engine::randomPlayout(Go::Board *board, Go::Color col)
{
  Go::Color coltomove;
  Go::Move *move;
  int passes=0; //not always true
  
  coltomove=col;
  
  //printf("random playout\n");
  
  while (!board->scoreable())
  {
    this->randomValidMove(board,coltomove,&move);
    //printf("random move at (%d,%d)\n",move->getX(),move->getY());
    board->makeMove(*move);
    coltomove=Go::otherColor(coltomove);
    if (move->isResign())
      break;
    if (move->isPass())
      passes++;
    else
      passes=0;
    if (passes>=2)
      break;
  }
}


