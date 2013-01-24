#ifndef DEF_OAKFOAM_DECISIONTREE_H
#define DEF_OAKFOAM_DECISIONTREE_H

#include <string>
#include <vector>

/** Decision Tree for Feature Ensemble Method. */
class DecisionTree
{
  public:
    ~DecisionTree();

    static DecisionTree *parseString(std::string rawdata);
    static DecisionTree *loadFile(std::string filename);
  
  private:
    class Range
    {
      public:
        Range(float s, float e, float v, Range *l, Range *r);
        Range(float s, float e, float v);
        ~Range();

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

      private:
        std::string label;
        std::vector<std::string> *attrs;
        Range *range;
    };

    class Stats
    {
      public:
        Stats(std::vector<StatPerm*> *sp);
        ~Stats();

      private:
        std::vector<StatPerm*> *statperms;
    };

    class Node
    {
      public:
        Node(Stats *s, float w);
        ~Node();

        Stats *getStats() { return stats; };
        float getWeight() { return weight; };

      private:
        Stats *stats;
        float weight;
    };

    DecisionTree(std::vector<std::string> *a, DecisionTree::Node *r);

    std::vector<std::string> *attrs;
    Node *root;

    static std::string stripWhitespace(std::string in);
    static std::vector<std::string> *parseAttrs(std::string data, unsigned int &pos);
    static Node *parseNode(std::string data, unsigned int &pos);
    static Stats *parseStats(std::string data, unsigned int &pos);
    static std::vector<StatPerm*> *parseStatPerms(std::string data, unsigned int &pos);
    static Range *parseRange(std::string data, unsigned int &pos);
    static float *parseNumber(std::string data, unsigned int &pos);
    static bool isText(char c);
};

#endif
