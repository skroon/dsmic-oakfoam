#ifndef DEF_OAKFOAM_DECISIONTREE_H
#define DEF_OAKFOAM_DECISIONTREE_H

#include <string>
#include <list>
#include <vector>
#include "Go.h"

/** Decision Tree for Feature Ensemble Method. */
class DecisionTree
{
  public:
    enum Type
    {
      SPARSE
    };

    ~DecisionTree();

    std::string toString();

    Type getType() { return type; };
    float getWeight(Go::Board *board, Go::Move move, bool updatestats = false);

    static DecisionTree *parseString(std::string rawdata);
    static DecisionTree *loadFile(std::string filename);
  
  private:
    class Range
    {
      public:
        Range(float s, float e, float v, Range *l, Range *r);
        Range(float s, float e, float v);
        ~Range();

        std::string toString(int indent);

      private:
        float start, end, val;
        Range *left;
        Range *right;
    };

    class StatPerm
    {
      public:
        StatPerm(std::string l, std::vector<std::string> *a, Range *r);
        ~StatPerm();

        std::string toString(int indent);

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

        std::string toString(int indent);

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
        ~Option();

        std::string toString(int indent);
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
        ~Query();

        std::string toString(int indent);
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

        std::string toString(int indent);
        bool isRoot() { return parent==NULL; };
        Option *getParent() { return parent; };
        void setParent(Option *p) { parent = p; };
        float getWeight() { return weight; };
        bool isLeaf() { return query == NULL; };
        Query *getQuery() { return query; };

        void populateEmptyStats(Type type, unsigned int maxnode = 0);

      private:
        Option *parent;
        Stats *stats;
        Query *query;
        float weight;
    };

    Type type;
    std::vector<std::string> *attrs;
    Node *root;

    DecisionTree(Type t, std::vector<std::string> *a, DecisionTree::Node *r);

    float getSparseWeight(Go::Board *board, Go::Move move, bool updatestats);
    std::list<Node*> *getSparseLeafNodes(Node *node, Go::Board *board, std::vector<int> *stones, bool invert, bool updatestats);

    static float combineNodeWeights(std::list<Node*> *nodes);
    static int getDistance(Go::Board *board, int p1, int p2);

    static std::string stripWhitespace(std::string in);
    static std::vector<std::string> *parseAttrs(std::string data, unsigned int &pos);
    static Node *parseNode(Type type, std::string data, unsigned int &pos);
    static Stats *parseStats(std::string data, unsigned int &pos);
    static std::vector<StatPerm*> *parseStatPerms(std::string data, unsigned int &pos);
    static Range *parseRange(std::string data, unsigned int &pos);
    static std::vector<Option*> *parseOptions(Type type, std::string data, unsigned int &pos);
    static float *parseNumber(std::string data, unsigned int &pos);
    static bool isText(char c);
};

#endif
