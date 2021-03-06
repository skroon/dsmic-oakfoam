#ifndef DEF_OAKFOAM_DECISIONTREE_H
#define DEF_OAKFOAM_DECISIONTREE_H

#include <string>
#include <list>
#include <vector>
#include "Go.h"
//from "Parameters.h":
class Parameters;

/** Decision Tree for Feature Ensemble Method. */
class DecisionTree
{
  public:
    enum Type
    {
      SPARSE
    };

    ~DecisionTree();

    std::string toString(bool ignorestats = false, int leafoffset = 0);

    Type getType() { return type; };
    std::vector<std::string> *getAttrs() { return attrs; };
    float getWeight(Go::Board *board, Go::Move move, bool updatetree = false);
    std::list<int> *getLeafIds(Go::Board *board, Go::Move move);
    void updateLeafIds();
    int getLeafCount() { return leafmap.size(); };
    void setLeafWeight(int id, float w);
    void updateDescent(Go::Board *board, Go::Move move);
    void updateDescent(Go::Board *board);
    void getTreeStats(int &treenodes, int &leaves, int &maxdepth, float &avgdepth, int &maxnodes, float &avgnodes);
    std::string getLeafPath(int id);

    static std::list<DecisionTree*> *parseString(Parameters *params, std::string rawdata, unsigned long pos = 0);
    static std::list<DecisionTree*> *loadFile(Parameters *params, std::string filename);
    static bool saveFile(std::list<DecisionTree*> *trees, std::string filename, bool ignorestats = false);

    static float getCollectionWeight(std::list<DecisionTree*> *trees, Go::Board *board, Go::Move move, bool updatetree = false);
    static std::list<int> *getCollectionLeafIds(std::list<DecisionTree*> *trees, Go::Board *board, Go::Move move);
    static int getCollectionLeafCount(std::list<DecisionTree*> *trees);
    static void setCollectionLeafWeight(std::list<DecisionTree*> *trees, int id, float w);
    static void collectionUpdateDescent(std::list<DecisionTree*> *trees, Go::Board *board);
    static std::string getCollectionLeafPath(std::list<DecisionTree*> *trees, int id);
  
  private:
    class Range
    {
      public:
        Range(int s, int e, int v, Range *l, Range *r);
        Range(int s, int e, int v = 0);
        ~Range();

        std::string toString(int indent = 0);
        bool isRoot() { return parent==NULL; };
        Range *getParent() { return parent; };
        void setParent(Range *p) { parent = p; };
        bool isTerminal() { return left==NULL && right==NULL; };
        int getStart() { return start; };
        int getEnd() { return end; };
        void addVal(int v, int div);
        int getThisVal() { return val; };
        float getExpectedMedian() { return this->getExpectedMedian(0,0); };
        float getExpectedPercentageLessThan(int v);
        float getExpectedPercentageEquals(int v);

      private:
        int start, end, val;
        Range *parent;
        Range *left;
        Range *right;

        float getExpectedMedian(float vl, float vr);
    };

    class StatPerm
    {
      public:
        StatPerm(std::string l, std::vector<std::string> *a, Range *r);
        ~StatPerm();

        std::string toString(int indent = 0);
        std::string getLabel() { return label; };
        std::vector<std::string> *getAttrs() { return attrs; };
        Range *getRange() { return range; };

      private:
        std::string label;
        std::vector<std::string> *attrs;
        Range *range;
    };

    class Stats
    {
      public:
        Stats(Type type, unsigned int maxnode);
        Stats(std::vector<StatPerm*> *sp);
        ~Stats();

        std::string toString(int indent = 0);
        std::vector<StatPerm*> *getStatPerms() { return statperms; };

      private:
        std::vector<StatPerm*> *statperms;
    };

    class Node;
    class Query;

    class Option
    {
      public:
        Option(std::string l, Node *n);
        Option(Type type, std::string l, unsigned int maxnode);
        ~Option();

        std::string toString(int indent = 0, bool ignorestats = false, int leafoffset = 0);
        Query *getParent() { return parent; };
        void setParent(Query *p) { parent = p; };
        std::string getLabel() { return label; };
        Node *getNode() { return node; };

      private:
        Query *parent;
        std::string label;
        Node *node;
    };

    class Query
    {
      public:
        Query(std::string l, std::vector<std::string> *a, std::vector<Option*> *o);
        Query(Type type, std::string l, std::vector<std::string> *a, unsigned int maxnode);
        ~Query();

        std::string toString(int indent = 0, bool ignorestats = false, int leafoffset = 0);
        Node *getParent() { return parent; };
        void setParent(Node *p) { parent = p; };
        std::string getLabel() { return label; };
        std::vector<std::string> *getAttrs() { return attrs; };
        std::vector<Option*> *getOptions() { return options; };

      private:
        Node *parent;
        std::string label;
        std::vector<std::string> *attrs;
        std::vector<Option*> *options;
    };

    class Node
    {
      public:
        Node(Stats *s, Query *q);
        Node(Stats *s, float w);
        ~Node();

        std::string toString(int indent = 0, bool ignorestats = false, int leafoffset = 0);
        bool isRoot() { return parent==NULL; };
        Option *getParent() { return parent; };
        void setParent(Option *p) { parent = p; };
        Stats *getStats() { return stats; };
        float getWeight() { return weight; };
        void setWeight(float w) { weight = w; };
        bool isLeaf() { return query == NULL; };
        int getLeafId() { return leafid; };
        Query *getQuery() { return query; };
        void setQuery(Query *q) { query = q; };
        std::string getPath();

        void populateEmptyStats(Type type, unsigned int maxnode = 0);
        void populateLeafIds(std::vector<Node*> &leafmap);
        void getTreeStats(Type type, int depth, int nodes, int &treenodes, int &leaves, int &maxdepth, int &sumdepth, int &maxnodes, int &sumnodes);

      private:
        Option *parent;
        Stats *stats;
        Query *query;
        int leafid;
        float weight;
    };

    Parameters *params;
    Type type;
    std::vector<std::string> *attrs;
    Node *root;
    std::vector<Node*> leafmap;

    DecisionTree(Parameters *p, Type t, std::vector<std::string> *a, DecisionTree::Node *r);

    std::list<Node*> *getLeafNodes(Go::Board *board, Go::Move move, bool updatetree);
    std::list<Node*> *getSparseLeafNodes(Node *node, Go::Board *board, std::vector<int> *stones, bool invert, bool updatetree);
    bool updateSparseNode(Node *node, Go::Board *board, std::vector<int> *stones, bool invert);
    unsigned int getMaxNode(Node *node);

    static float combineNodeWeights(std::list<Node*> *nodes);
    static int getDistance(Go::Board *board, int p1, int p2);
    static float percentageToVal(float p);

    static std::string stripWhitespace(std::string in);
    static std::string stripComments(std::string in);
    static std::vector<std::string> *parseAttrs(std::string data, unsigned long &pos);
    static Node *parseNode(Type type, std::string data, unsigned long &pos);
    static Stats *parseStats(std::string data, unsigned long &pos);
    static std::vector<StatPerm*> *parseStatPerms(std::string data, unsigned long &pos);
    static Range *parseRange(std::string data, unsigned long &pos);
    static std::vector<Option*> *parseOptions(Type type, std::string data, unsigned long &pos);
    static float *parseNumber(std::string data, unsigned long &pos);
    static bool isText(char c);
};

#endif
