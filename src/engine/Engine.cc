#include "Engine.h"

#include <cstdlib>
#include <cmath>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <boost/timer.hpp>
#include <boost/lexical_cast.hpp>
#ifdef HAVE_MPI
  #define MPIRANK0_ONLY(__body) {if (mpirank==0) { __body }}
#else
  #define MPIRANK0_ONLY(__body) { __body }
#endif
#include "Pattern.h"
#include "DecisionTree.h"
#ifdef HAVE_WEB
  #include "../web/Web.h"
#endif


Engine::Engine(Gtp::Engine *ge, std::string ln) : params(new Parameters())
{
  gtpe=ge;
  longname=ln;
  #ifdef HAVE_WEB
    web=NULL;
  #endif
  
  params->engine=this;
  
  #ifdef HAVE_MPI
    MPI::COMM_WORLD.Set_errhandler(MPI::ERRORS_ARE_FATAL);
    longname+=" (MPI)";
    mpiworldsize=MPI::COMM_WORLD.Get_size();
    mpirank=MPI::COMM_WORLD.Get_rank();
    mpihashtable.clear();
  #endif
  
  boardsize=9;
  params->board_size=boardsize;
  currentboard=new Go::Board(boardsize);
  komi=7.5;
  komi_handicap=0;

  params->tree_instances=0;
  
  zobristtable=new Go::ZobristTable(params,boardsize,ZOBRIST_HASH_SEED);
  
  params->addParameter("other","rand_seed",&(params->rand_seed),Random::makeSeed(),&Engine::updateParameterWrapper,this);
  
  std::list<std::string> *mpoptions=new std::list<std::string>();
  mpoptions->push_back("playout");
  mpoptions->push_back("1-ply");
  mpoptions->push_back("uct");
  params->addParameter("general","move_policy",&(params->move_policy_string),mpoptions,"uct",&Engine::updateParameterWrapper,this);
  params->move_policy=Parameters::MP_UCT;
  
  params->addParameter("general","book_use",&(params->book_use),BOOK_USE);
  
  params->addParameter("general","thread_count",&(params->thread_count),THREAD_COUNT,&Engine::updateParameterWrapper,this);
  params->addParameter("general","memory_usage_max",&(params->memory_usage_max),MEMORY_USAGE_MAX);
  
  params->addParameter("general","playouts_per_move",&(params->playouts_per_move),PLAYOUTS_PER_MOVE,&Engine::updateParameterWrapper,this);
  params->addParameter("general","playouts_per_move_max",&(params->playouts_per_move_max),PLAYOUTS_PER_MOVE_MAX);
  params->addParameter("general","playouts_per_move_min",&(params->playouts_per_move_min),PLAYOUTS_PER_MOVE_MIN);
  
  params->addParameter("playout","playout_criticality_random_n",&(params->playout_criticality_random_n),PLAYOUT_CRITICALITY_RANDOM_N);
  params->addParameter("playout","playout_poolrave_enabled",&(params->playout_poolrave_enabled),PLAYOUT_POOLRAVE_ENABLED);
  params->addParameter("playout","playout_poolrave_criticality",&(params->playout_poolrave_criticality),PLAYOUT_POOLRAVE_CRITICALITY);
  params->addParameter("playout","playout_poolrave_p",&(params->playout_poolrave_p),PLAYOUT_POOLRAVE_P);
  params->addParameter("playout","playout_poolrave_k",&(params->playout_poolrave_k),PLAYOUT_POOLRAVE_K);
  params->addParameter("playout","playout_poolrave_min_playouts",&(params->playout_poolrave_min_playouts),PLAYOUT_POOLRAVE_MIN_PLAYOUTS);
  params->addParameter("playout","playout_lgrf2_enabled",&(params->playout_lgrf2_enabled),PLAYOUT_LGRF2_ENABLED);
  params->addParameter("playout","playout_lgrf1_enabled",&(params->playout_lgrf1_enabled),PLAYOUT_LGRF1_ENABLED);
  params->addParameter("playout","playout_lgrf_local",&(params->playout_lgrf_local),PLAYOUT_LGRF_LOCAL);
  params->addParameter("playout","playout_lgrf2_safe_enabled",&(params->playout_lgrf2_safe_enabled),PLAYOUT_LGRF2_SAFE_ENABLED);
  params->addParameter("playout","playout_lgrf1_safe_enabled",&(params->playout_lgrf1_safe_enabled),PLAYOUT_LGRF1_SAFE_ENABLED);
  params->addParameter("playout","playout_lgrf1o_enabled",&(params->playout_lgrf1o_enabled),PLAYOUT_LGRF1O_ENABLED);
  params->addParameter("playout","playout_avoid_lbrf1_p",&(params->playout_avoid_lbrf1_p),PLAYOUT_AVOID_LBRF1_P);
  params->addParameter("playout","playout_avoid_lbmf_p",&(params->playout_avoid_lbmf_p),PLAYOUT_AVOID_LBMF_P);
  params->addParameter("playout","playout_avoid_lbrf1_p2",&(params->playout_avoid_lbrf1_p2),PLAYOUT_AVOID_LBRF1_P2);
  params->addParameter("playout","playout_avoid_lbmf_p2",&(params->playout_avoid_lbmf_p2),PLAYOUT_AVOID_LBMF_P2);
  params->addParameter("playout","playout_lgpf_enabled",&(params->playout_lgpf_enabled),PLAYOUT_LGPF_ENABLED);
  params->addParameter("playout","playout_atari_enabled",&(params->playout_atari_enabled),PLAYOUT_ATARI_ENABLED);
  params->addParameter("playout","playout_lastatari_p",&(params->playout_lastatari_p),PLAYOUT_LASTATARI_P);
  params->addParameter("playout","playout_lastatari_leavedouble",&(params->playout_lastatari_leavedouble),PLAYOUT_LASTATARI_LEAVEDOUBLE);
  params->addParameter("playout","playout_lastatari_captureattached_p",&(params->playout_lastatari_captureattached_p),PLAYOUT_LASTATARI_CAPTUREATTACHED);
  params->addParameter("playout","playout_lastcapture_enabled",&(params->playout_lastcapture_enabled),PLAYOUT_LASTCAPTURE_ENABLED);
  params->addParameter("playout","playout_last2libatari_enabled",&(params->playout_last2libatari_enabled),PLAYOUT_LAST2LIBATARI_ENABLED);
  params->addParameter("playout","playout_last2libatari_complex",&(params->playout_last2libatari_complex),PLAYOUT_LAST2LIBATARI_COMPLEX);
  params->addParameter("playout","playout_nakade_enabled",&(params->playout_nakade_enabled),PLAYOUT_NAKADE_ENABLED);
  params->addParameter("playout","playout_nakade4_enabled",&(params->playout_nakade4_enabled),PLAYOUT_NAKADE4_ENABLED);
  params->addParameter("playout","playout_nakade_bent4_enabled",&(params->playout_nakade_bent4_enabled),PLAYOUT_NAKADE_BENT4_ENABLED);
  params->addParameter("playout","playout_nakade5_enabled",&(params->playout_nakade5_enabled),PLAYOUT_NAKADE5_ENABLED);
  params->addParameter("playout","playout_nearby_enabled",&(params->playout_nearby_enabled),PLAYOUT_NEARBY_ENABLED);
  params->addParameter("playout","playout_fillboard_enabled",&(params->playout_fillboard_enabled),PLAYOUT_FILLBOARD_ENABLED);
  params->addParameter("playout","playout_fillboard_n",&(params->playout_fillboard_n),PLAYOUT_FILLBOARD_N);
  params->addParameter("playout","playout_circreplace_enabled",&(params->playout_circreplace_enabled),PLAYOUT_CIRCREPLACE_ENABLED);
  params->addParameter("playout","playout_fillboard_bestcirc_enabled",&(params->playout_fillboard_bestcirc_enabled),PLAYOUT_FILLBOARD_BESTCIRC_ENABLED);
  params->addParameter("playout","playout_randomquick_bestcirc_n",&(params->playout_randomquick_bestcirc_n),PLAYOUT_RANDOMQUICK_BESTCIRC_N);
  params->addParameter("playout","playout_random_weight_territory_n",&(params->playout_random_weight_territory_n),PLAYOUT_RANDOM_WEIGHT_TERRITORY_N);
  params->addParameter("playout","playout_random_weight_territory_f0",&(params->playout_random_weight_territory_f0),PLAYOUT_RANDOM_WEIGHT_TERRITORY_F0);
  params->addParameter("playout","playout_random_weight_territory_f1",&(params->playout_random_weight_territory_f1),PLAYOUT_RANDOM_WEIGHT_TERRITORY_F1);
  params->addParameter("playout","playout_random_weight_territory_f",&(params->playout_random_weight_territory_f),PLAYOUT_RANDOM_WEIGHT_TERRITORY_F);
  params->addParameter("playout","playout_circpattern_n",&(params->playout_circpattern_n),PLAYOUT_CIRCPATTERN_N);
  params->addParameter("playout","playout_patterns_p",&(params->playout_patterns_p),PLAYOUT_PATTERNS_P);
  params->addParameter("playout","playout_patterns_gammas_p",&(params->playout_patterns_gammas_p),PLAYOUT_PATTERNS_GAMMAS_P);
  params->addParameter("playout","playout_anycapture_p",&(params->playout_anycapture_p),PLAYOUT_ANYCAPTURE_P);
  params->addParameter("playout","playout_features_enabled",&(params->playout_features_enabled),PLAYOUT_FEATURES_ENABLED);
  params->addParameter("playout","playout_features_incremental",&(params->playout_features_incremental),PLAYOUT_FEATURES_INCREMENTAL);
  params->addParameter("playout","playout_random_chance",&(params->playout_random_chance),PLAYOUT_RANDOM_CHANCE);
  params->addParameter("playout","playout_random_approach_p",&(params->playout_random_approach_p),PLAYOUT_RANDOM_APPROACH_P);
  params->addParameter("playout","playout_avoid_selfatari",&(params->playout_avoid_selfatari),PLAYOUT_AVOID_SELFATARI);
  params->addParameter("playout","playout_avoid_selfatari_size",&(params->playout_avoid_selfatari_size),PLAYOUT_AVOID_SELFATARI_SIZE);
  params->addParameter("playout","playout_avoid_selfatari_complex",&(params->playout_avoid_selfatari_complex),PLAYOUT_AVOID_SELFATARI_COMPLEX);
  params->addParameter("playout","playout_useless_move",&(params->playout_useless_move),PLAYOUT_USELESS_MOVE);
  params->addParameter("playout","playout_order",&(params->playout_order),PLAYOUT_ORDER);
  params->addParameter("playout","playout_mercy_rule_enabled",&(params->playout_mercy_rule_enabled),PLAYOUT_MERCY_RULE_ENABLED);
  params->addParameter("playout","playout_mercy_rule_factor",&(params->playout_mercy_rule_factor),PLAYOUT_MERCY_RULE_FACTOR);
  params->addParameter("playout","playout_fill_weak_eyes",&(params->playout_fill_weak_eyes),PLAYOUT_FILL_WEAK_EYES);
  

  params->addParameter("playout","test_p1",&(params->test_p1),0.0);
  params->addParameter("playout","test_p2",&(params->test_p2),0.0);
  params->addParameter("playout","test_p3",&(params->test_p3),0.0);
  params->addParameter("playout","test_p4",&(params->test_p4),0.0);
  params->addParameter("playout","test_p5",&(params->test_p5),1.0);
  params->addParameter("playout","test_p6",&(params->test_p6),0.0);
  params->addParameter("playout","test_p7",&(params->test_p7),0.0);
  params->addParameter("playout","test_p8",&(params->test_p8),0.0);
  params->addParameter("playout","test_p9",&(params->test_p9),1.0);
  params->addParameter("playout","test_p10",&(params->test_p10),0.0);
  params->addParameter("playout","test_p11",&(params->test_p11),0.0);
  params->addParameter("playout","test_p12",&(params->test_p12),1.0);
  params->addParameter("playout","test_p13",&(params->test_p13),1.0);
  params->addParameter("playout","test_p14",&(params->test_p14),1.0);
  params->addParameter("playout","test_p15",&(params->test_p15),1.0);
  params->addParameter("playout","test_p16",&(params->test_p16),1.0);
  params->addParameter("playout","test_p17",&(params->test_p17),1.0);
  params->addParameter("playout","test_p18",&(params->test_p18),1.0);
  params->addParameter("playout","test_p19",&(params->test_p19),1.0);
  params->addParameter("playout","test_p20",&(params->test_p20),1.0);
 
  params->addParameter("tree","ucb_c",&(params->ucb_c),UCB_C);
  params->addParameter("tree","ucb_init",&(params->ucb_init),UCB_INIT);

  params->addParameter("tree","bernoulli_a",&(params->bernoulli_a),BERNOULLI_A);
  params->addParameter("tree","bernoulli_b",&(params->bernoulli_b),BERNOULLI_B);
  params->addParameter("tree","weight_score",&(params->weight_score),WEIGHT_SCORE);
  params->addParameter("tree","random_f",&(params->random_f),RANDOM_F);

  params->addParameter("tree","rave_moves",&(params->rave_moves),RAVE_MOVES);
  params->addParameter("tree","rave_init_wins",&(params->rave_init_wins),RAVE_INIT_WINS);
  params->addParameter("tree","uct_preset_rave_f",&(params->uct_preset_rave_f),UCT_PRESET_RAVE_F);
  params->addParameter("tree","rave_skip",&(params->rave_skip),RAVE_SKIP);
  params->addParameter("tree","rave_moves_use",&(params->rave_moves_use),RAVE_MOVES_USE);
  params->addParameter("tree","rave_only_first_move",&(params->rave_only_first_move),RAVE_ONLY_FIRST_MOVE);
  
  params->addParameter("tree","uct_expand_after",&(params->uct_expand_after),UCT_EXPAND_AFTER);
  params->addParameter("tree","uct_keep_subtree",&(params->uct_keep_subtree),UCT_KEEP_SUBTREE,&Engine::updateParameterWrapper,this);
  params->addParameter("tree","uct_symmetry_use",&(params->uct_symmetry_use),UCT_SYMMETRY_USE,&Engine::updateParameterWrapper,this);
  if (params->uct_symmetry_use)
    currentboard->turnSymmetryOn();
  else
    currentboard->turnSymmetryOff();
  params->addParameter("tree","uct_virtual_loss",&(params->uct_virtual_loss),UCT_VIRTUAL_LOSS);
  params->addParameter("tree","uct_lock_free",&(params->uct_lock_free),UCT_LOCK_FREE);
  params->addParameter("tree","uct_atari_prior",&(params->uct_atari_prior),UCT_ATARI_PRIOR);
  params->addParameter("tree","uct_playoutmove_prior",&(params->uct_playoutmove_prior),UCT_PLAYOUTMOVE_PRIOR);
  params->addParameter("tree","uct_pattern_prior",&(params->uct_pattern_prior),UCT_PATTERN_PRIOR);
  params->addParameter("tree","uct_progressive_widening_enabled",&(params->uct_progressive_widening_enabled),UCT_PROGRESSIVE_WIDENING_ENABLED);
  params->addParameter("tree","uct_progressive_widening_init",&(params->uct_progressive_widening_init),UCT_PROGRESSIVE_WIDENING_INIT);
  params->addParameter("tree","uct_progressive_widening_a",&(params->uct_progressive_widening_a),UCT_PROGRESSIVE_WIDENING_A);
  params->addParameter("tree","uct_progressive_widening_b",&(params->uct_progressive_widening_b),UCT_PROGRESSIVE_WIDENING_B);
  params->addParameter("tree","uct_progressive_widening_c",&(params->uct_progressive_widening_c),UCT_PROGRESSIVE_WIDENING_C);
  params->addParameter("tree","uct_progressive_widening_count_wins",&(params->uct_progressive_widening_count_wins),UCT_PROGRESSIVE_WIDENING_COUNT_WINS);
  params->addParameter("tree","uct_points_bonus",&(params->uct_points_bonus),UCT_POINTS_BONUS);
  params->addParameter("tree","uct_length_bonus",&(params->uct_length_bonus),UCT_LENGTH_BONUS);
  params->addParameter("tree","uct_progressive_bias_enabled",&(params->uct_progressive_bias_enabled),UCT_PROGRESSIVE_BIAS_ENABLED);
  params->addParameter("tree","uct_progressive_bias_h",&(params->uct_progressive_bias_h),UCT_PROGRESSIVE_BIAS_H);
  params->addParameter("tree","uct_progressive_bias_scaled",&(params->uct_progressive_bias_scaled),UCT_PROGRESSIVE_BIAS_SCALED);
  params->addParameter("tree","uct_progressive_bias_relative",&(params->uct_progressive_bias_relative),UCT_PROGRESSIVE_BIAS_RELATIVE);
  params->addParameter("tree","uct_progressive_bias_moves",&(params->uct_progressive_bias_moves),UCT_PROGRESSIVE_BIAS_MOVES);
  params->addParameter("tree","uct_progressive_bias_exponent",&(params->uct_progressive_bias_exponent),UCT_PROGRESSIVE_BIAS_EXPONENT);
  params->addParameter("tree","uct_criticality_urgency_factor",&(params->uct_criticality_urgency_factor),UCT_CRITICALITY_URGENCY_FACTOR);
  params->addParameter("tree","uct_criticality_urgency_decay",&(params->uct_criticality_urgency_decay),UCT_CRITICALITY_URGENCY_DECAY);
  params->addParameter("tree","uct_criticality_unprune_factor",&(params->uct_criticality_unprune_factor),UCT_CRITICALITY_UNPRUNE_FACTOR);
  params->addParameter("tree","uct_criticality_unprune_multiply",&(params->uct_criticality_unprune_multiply),UCT_CRITICALITY_UNPRUNE_MULTIPLY);
  params->addParameter("tree","uct_criticality_min_playouts",&(params->uct_criticality_min_playouts),UCT_CRITICALITY_MIN_PLAYOUTS);
  params->addParameter("tree","uct_criticality_siblings",&(params->uct_criticality_siblings),UCT_CRITICALITY_SIBLINGS);
  params->addParameter("tree","uct_criticality_rave_unprune_factor",&(params->uct_criticality_rave_unprune_factor),UCT_CRITICALITY_RAVE_UNPRUNE_FACTOR);
  params->addParameter("tree","uct_prior_unprune_factor",&(params->uct_prior_unprune_factor),UCT_PRIOR_UNPRUNE_FACTOR);
  params->addParameter("tree","uct_rave_unprune_factor",&(params->uct_rave_unprune_factor),UCT_RAVE_UNPRUNE_FACTOR);
  params->addParameter("tree","uct_earlyrave_unprune_factor",&(params->uct_earlyrave_unprune_factor),UCT_EARLYRAVE_UNPRUNE_FACTOR);
  params->addParameter("tree","uct_rave_unprune_decay",&(params->uct_rave_unprune_decay),UCT_RAVE_UNPRUNE_DECAY);
  params->addParameter("tree","uct_rave_unprune_multiply",&(params->uct_rave_unprune_multiply),UCT_RAVE_UNPRUNE_MULTIPLY);
  params->addParameter("tree","uct_oldmove_unprune_factor",&(params->uct_oldmove_unprune_factor),UCT_OLDMOVE_UNPRUNE_FACTOR);
  params->addParameter("tree","uct_oldmove_unprune_factor_b",&(params->uct_oldmove_unprune_factor_b),UCT_OLDMOVE_UNPRUNE_FACTOR_B);
  params->addParameter("tree","uct_oldmove_unprune_factor_c",&(params->uct_oldmove_unprune_factor_c),UCT_OLDMOVE_UNPRUNE_FACTOR_C);
  params->addParameter("tree","uct_area_owner_factor_a",&(params->uct_area_owner_factor_a),UCT_AREA_OWNER_FACTOR_A);
  params->addParameter("tree","uct_area_owner_factor_b",&(params->uct_area_owner_factor_b),UCT_AREA_OWNER_FACTOR_B);
  params->addParameter("tree","uct_area_owner_factor_c",&(params->uct_area_owner_factor_c),UCT_AREA_OWNER_FACTOR_C);
  params->addParameter("tree","uct_reprune_factor",&(params->uct_reprune_factor),UCT_REPRUNE_FACTOR);
  params->addParameter("tree","uct_factor_circpattern",&(params->uct_factor_circpattern),UCT_FACTOR_CIRCPATTERN);
  params->addParameter("tree","uct_factor_circpattern_exponent",&(params->uct_factor_circpattern_exponent),UCT_FACTOR_CIRCPATTERN_EXPONENT);
  params->addParameter("tree","uct_circpattern_minsize",&(params->uct_circpattern_minsize),UCT_CIRCPATTERN_MINSIZE);
  params->addParameter("tree","uct_simple_pattern_factor",&(params->uct_simple_pattern_factor),UCT_SIMPLE_PATTERN_FACTOR);
  params->addParameter("tree","uct_atari_unprune",&(params->uct_atari_unprune),UCT_ATARI_UNPRUNE);
  params->addParameter("tree","uct_atari_unprune_exp",&(params->uct_atari_unprune_exp),UCT_ATARI_UNPRUNE_EXP);
  params->addParameter("tree","uct_danger_value",&(params->uct_atari_unprune_exp),UCT_DANGER_VALUE);
  
  params->addParameter("tree","uct_slow_update_interval",&(params->uct_slow_update_interval),UCT_SLOW_UPDATE_INTERVAL);
  params->uct_slow_update_last=0;
  params->addParameter("tree","uct_slow_debug_interval",&(params->uct_slow_debug_interval),UCT_SLOW_DEBUG_INTERVAL);
  params->uct_slow_debug_last=0;
  params->addParameter("tree","uct_stop_early",&(params->uct_stop_early),UCT_STOP_EARLY);
  params->addParameter("tree","uct_terminal_handling",&(params->uct_terminal_handling),UCT_TERMINAL_HANDLING);
  
  params->addParameter("tree","surewin_threshold",&(params->surewin_threshold),SUREWIN_THRESHOLD);
  params->surewin_expected=false;
  params->addParameter("tree","surewin_pass_bonus",&(params->surewin_pass_bonus),SUREWIN_PASS_BONUS);
  params->addParameter("tree","surewin_touchdead_bonus",&(params->surewin_touchdead_bonus),SUREWIN_TOUCHDEAD_BONUS);
  params->addParameter("tree","surewin_oppoarea_penalty",&(params->surewin_oppoarea_penalty),SUREWIN_OPPOAREA_PENALTY);
  
  params->addParameter("tree","resign_ratio_threshold",&(params->resign_ratio_threshold),RESIGN_RATIO_THRESHOLD);
  params->addParameter("tree","resign_move_factor_threshold",&(params->resign_move_factor_threshold),RESIGN_MOVE_FACTOR_THRESHOLD);
  
  params->addParameter("tree","territory_decayfactor",&(params->territory_decayfactor),TERRITORY_DECAYFACTOR);
  params->addParameter("tree","territory_threshold",&(params->territory_threshold),TERRITORY_THRESHOLD);
  
  params->addParameter("tree","uct_decay_alpha",&(params->uct_decay_alpha),UCT_DECAY_ALPHA);
  params->addParameter("tree","uct_decay_k",&(params->uct_decay_k),UCT_DECAY_K);
  params->addParameter("tree","uct_decay_m",&(params->uct_decay_m),UCT_DECAY_M);

  params->addParameter("tree","features_ladders",&(params->features_ladders),FEATURES_LADDERS);
  params->addParameter("tree","features_dt_use",&(params->features_dt_use),false);
  params->addParameter("tree","features_pass_no_move_for_lastdist",&(params->features_pass_no_move_for_lastdist),FEATURES_PASS_NO_MOVE_FOR_LASTDIST);

  params->addParameter("tree","dynkomi_enabled",&(params->dynkomi_enabled),true);
  
  params->addParameter("tree","mm_learn_enabled",&(params->mm_learn_enabled),false);
  params->addParameter("tree","mm_learn_delta",&(params->mm_learn_delta),MM_LEARN_DELTA);
  params->addParameter("tree","mm_learn_min_playouts",&(params->mm_learn_min_playouts),MM_LEARN_MIN_PLAYOUTS);

  params->addParameter("rules","rules_positional_superko_enabled",&(params->rules_positional_superko_enabled),RULES_POSITIONAL_SUPERKO_ENABLED);
  params->addParameter("rules","rules_superko_top_ply",&(params->rules_superko_top_ply),RULES_SUPERKO_TOP_PLY);
  params->addParameter("rules","rules_superko_prune_after",&(params->rules_superko_prune_after),RULES_SUPERKO_PRUNE_AFTER);
  params->addParameter("rules","rules_superko_at_playout",&(params->rules_superko_at_playout),RULES_SUPERKO_AT_PLAYOUT);
  params->addParameter("rules","rules_all_stones_alive",&(params->rules_all_stones_alive),RULES_ALL_STONES_ALIVE);
  params->addParameter("rules","rules_all_stones_alive_playouts",&(params->rules_all_stones_alive_playouts),RULES_ALL_STONES_ALIVE_PLAYOUTS);
  
  params->addParameter("time","time_k",&(params->time_k),TIME_K);
  params->addParameter("time","time_buffer",&(params->time_buffer),TIME_BUFFER);
  params->addParameter("time","time_move_minimum",&(params->time_move_minimum),TIME_MOVE_MINIMUM);
  params->addParameter("time","time_ignore",&(params->time_ignore),false);
  params->addParameter("time","time_move_max",&(params->time_move_max),TIME_MOVE_MAX);
  
  params->addParameter("general","pondering_enabled",&(params->pondering_enabled),PONDERING_ENABLED,&Engine::updateParameterWrapper,this);
  params->addParameter("general","pondering_playouts_max",&(params->pondering_playouts_max),PONDERING_PLAYOUTS_MAX);
  
  params->addParameter("other","live_gfx",&(params->livegfx_on),LIVEGFX_ON);
  params->addParameter("other","live_gfx_update_playouts",&(params->livegfx_update_playouts),LIVEGFX_UPDATE_PLAYOUTS);
  params->addParameter("other","live_gfx_delay",&(params->livegfx_delay),LIVEGFX_DELAY);
  
  params->addParameter("other","outputsgf_maxchildren",&(params->outputsgf_maxchildren),OUTPUTSGF_MAXCHILDREN);
  
  params->addParameter("other","debug",&(params->debug_on),DEBUG_ON);
  
  params->addParameter("other","interrupts_enabled",&(params->interrupts_enabled),INTERRUPTS_ENABLED,&Engine::updateParameterWrapper,this);
  params->addParameter("other","undo_enable",&(params->undo_enable),true);
  
  params->addParameter("other","features_only_small",&(params->features_only_small),false);
  params->addParameter("other","features_output_competitions",&(params->features_output_competitions),0.0);
  params->addParameter("other","features_output_competitions_mmstyle",&(params->features_output_competitions_mmstyle),false);
  params->addParameter("other","features_ordered_comparison",&(params->features_ordered_comparison),false);
  params->addParameter("other","features_circ_list",&(params->features_circ_list),0.0);
  params->addParameter("other","features_circ_list_size",&(params->features_circ_list_size),0);
  
  params->addParameter("other","auto_save_sgf",&(params->auto_save_sgf),false);
  params->addParameter("other","auto_save_sgf_prefix",&(params->auto_save_sgf_prefix),"");

  params->addParameter("other","dt_update_prob",&(params->dt_update_prob),0.00);
  params->addParameter("other","dt_split_after",&(params->dt_split_after),1000);
  params->addParameter("other","dt_split_threshold",&(params->dt_split_threshold),0.00);
  params->addParameter("other","dt_range_divide",&(params->dt_range_divide),10);
  params->addParameter("other","dt_solo_leaf",&(params->dt_solo_leaf),true);
  params->addParameter("other","dt_output_mm",&(params->dt_output_mm),0.00);
  params->addParameter("other","dt_ordered_comparison",&(params->dt_ordered_comparison),false);
  
  #ifdef HAVE_MPI
    params->addParameter("mpi","mpi_update_period",&(params->mpi_update_period),MPI_UPDATE_PERIOD);
    params->addParameter("mpi","mpi_update_depth",&(params->mpi_update_depth),MPI_UPDATE_DEPTH);
    params->addParameter("mpi","mpi_update_threshold",&(params->mpi_update_threshold),MPI_UPDATE_THRESHOLD);
  #endif
  
  patterntable=new Pattern::ThreeByThreeTable();
  patterntable->loadPatternDefaults();
  
  features=new Features(params);
  features->loadGammaDefaults();
  
  book=new Book(params);
  
  time=new Time(params,0);
  
  playout=new Playout(params);
  
  movehistory=new std::list<Go::Move>();
  moveexplanations=new std::list<std::string>();
  hashtree=new Go::ZobristTree();
  
  territorymap=new Go::TerritoryMap(boardsize);
  correlationmap=new Go::ObjectBoard<Go::CorrelationData>(boardsize);
  
  blackOldMoves=new float[currentboard->getPositionMax()];
  whiteOldMoves=new float[currentboard->getPositionMax()];
  for (int i=0;i<currentboard->getPositionMax();i++)
  {
    blackOldMoves[i]=0;
    whiteOldMoves[i]=0;
  }
  blackOldMean=0.5;
  whiteOldMean=0.5;
  blackOldMovesNum=0;
  whiteOldMovesNum=0;
  
  this->addGtpCommands();
  
  movetree=NULL;
  this->clearMoveTree();

  isgamefinished=false;
  
  params->cleanup_in_progress=false;
  
  params->uct_last_r2=0;
  
  params->thread_job=Parameters::TJ_GENMOVE;
  threadpool = new Worker::Pool(params);
  
  #ifdef HAVE_MPI
    this->mpiBuildDerivedTypes();
    
    if (mpirank==0)
    {
      bool errorsyncing=false;
      
      for (int i=1;i<mpiworldsize;i++)
      {
        std::string data=this->mpiRecvString(i);
        if (data!=VERSION)
          errorsyncing=true;
      }
      
      if (errorsyncing)
        gtpe->getOutput()->printfDebug("FATAL ERROR! could not sync mpi\n");
      else
        gtpe->getOutput()->printfDebug("mpi synced world of size: %d\n",mpiworldsize);
      mpisynced=!errorsyncing;
    }
    else
    {
      this->mpiSendString(0,VERSION);
      mpisynced=true;
    }
  #endif
}

Engine::~Engine()
{
  this->gameFinished();
  #ifdef HAVE_MPI
    MPIRANK0_ONLY(this->mpiBroadcastCommand(Engine::MPICMD_QUIT););
    this->mpiFreeDerivedTypes();
  #endif
  delete threadpool;
  delete features;
  delete patterntable;
  if (movetree!=NULL)
    delete movetree;
  delete currentboard;
  delete movehistory;
  delete moveexplanations;
  delete hashtree;
  delete params;
  delete time;
  delete book;
  delete zobristtable;
  delete playout;
  delete territorymap;
  delete correlationmap;
  delete blackOldMoves;
  delete whiteOldMoves;
  for (std::list<DecisionTree*>::iterator iter=decisiontrees.begin();iter!=decisiontrees.end();++iter)
  {
    delete (*iter);
  }
}

void Engine::run(bool web_inf, std::string web_addr, int web_port)
{
  bool use_web=false;

  #ifdef HAVE_MPI
    if (mpirank==0)
    {
      if (mpisynced)
      {
        if (web_inf)
          use_web=true;
        else
          gtpe->run();
      }
    }
    else
      this->mpiCommandHandler();
  #else
    if (web_inf)
      use_web=true;
    else
      gtpe->run();
  #endif

  if (use_web)
  {
    #ifdef HAVE_WEB
      web=new Web(this,web_addr,web_port);
      web->run();
      delete web;
    #endif
  }
}

void Engine::postCmdLineArgs(bool book_autoload)
{
  params->rand_seed=threadpool->getThreadZero()->getSettings()->rand->getSeed();
  #ifdef HAVE_MPI
    gtpe->getOutput()->printfDebug("seed of rank %d: %lu\n",mpirank,params->rand_seed);
  #else
    gtpe->getOutput()->printfDebug("seed: %lu\n",params->rand_seed);
  #endif
  if (book_autoload)
  {
    bool loaded=false;
    if (!loaded)
    {
      std::string filename="book.dat";
      MPIRANK0_ONLY(gtpe->getOutput()->printfDebug("loading opening book from '%s'... ",filename.c_str()););
      loaded=book->loadFile(filename);
      MPIRANK0_ONLY(
        if (loaded)
          gtpe->getOutput()->printfDebug("done\n");
        else
          gtpe->getOutput()->printfDebug("error\n");
      );
    }
    #ifdef TOPSRCDIR
      if (!loaded)
      {
        std::string filename=TOPSRCDIR "/book.dat";
        MPIRANK0_ONLY(gtpe->getOutput()->printfDebug("loading opening book from '%s'... ",filename.c_str()););
        loaded=book->loadFile(filename);
        MPIRANK0_ONLY(
          if (loaded)
            gtpe->getOutput()->printfDebug("done\n");
          else
            gtpe->getOutput()->printfDebug("error\n");
        );
      }
    #endif
    
    MPIRANK0_ONLY(
      if (!loaded)
        gtpe->getOutput()->printfDebug("no opening book loaded\n");
    );
  }
}

void Engine::updateParameter(std::string id)
{
  if (id=="move_policy")
  {
    if (params->move_policy_string=="uct")
      params->move_policy=Parameters::MP_UCT;
    else if (params->move_policy_string=="1-ply")
      params->move_policy=Parameters::MP_ONEPLY;
    else
      params->move_policy=Parameters::MP_PLAYOUT;
  }
  else if (id=="uct_keep_subtree")
  {
    this->clearMoveTree();
  }
  else if (id=="uct_symmetry_use")
  {
    if (params->uct_symmetry_use)
      currentboard->turnSymmetryOn();
    else
    {
      currentboard->turnSymmetryOff();
      this->clearMoveTree();
    }
  }
  else if (id=="rand_seed")
  {
    threadpool->setRandomSeeds(params->rand_seed);
    params->rand_seed=threadpool->getThreadZero()->getSettings()->rand->getSeed();
  }
  else if (id=="interrupts_enabled")
  {
    gtpe->setWorkerEnabled(params->interrupts_enabled);
  }
  else if (id=="pondering_enabled")
  {
    gtpe->setPonderEnabled(params->pondering_enabled);
  }
  else if (id=="thread_count")
  {
    if (threadpool->getSize()!=params->thread_count)
    {
      delete threadpool;
      threadpool = new Worker::Pool(params);
    }
  }
  else if (id=="playouts_per_move")
  {
    if (params->playouts_per_move>params->playouts_per_move_max)
      params->playouts_per_move_max=params->playouts_per_move;
    if (params->playouts_per_move<params->playouts_per_move_min)
      params->playouts_per_move_min=params->playouts_per_move;
  }
}

void Engine::addGtpCommands()
{
  gtpe->addFunctionCommand("boardsize",this,&Engine::gtpBoardSize);
  gtpe->addFunctionCommand("clear_board",this,&Engine::gtpClearBoard);
  gtpe->addFunctionCommand("komi",this,&Engine::gtpKomi);
  gtpe->addFunctionCommand("play",this,&Engine::gtpPlay);
  gtpe->addFunctionCommand("genmove",this,&Engine::gtpGenMove);
  gtpe->addFunctionCommand("reg_genmove",this,&Engine::gtpRegGenMove);
  gtpe->addFunctionCommand("kgs-genmove_cleanup",this,&Engine::gtpGenMoveCleanup);
  gtpe->addFunctionCommand("showboard",this,&Engine::gtpShowBoard);
  gtpe->addFunctionCommand("final_score",this,&Engine::gtpFinalScore);
  gtpe->addFunctionCommand("final_status_list",this,&Engine::gtpFinalStatusList);
  gtpe->addFunctionCommand("undo",this,&Engine::gtpUndo);
  gtpe->addFunctionCommand("kgs-chat",this,&Engine::gtpChat);
  gtpe->addFunctionCommand("kgs-game_over",this,&Engine::gtpGameOver);
  gtpe->addFunctionCommand("echo",this,&Engine::gtpEcho);
  gtpe->addFunctionCommand("place_free_handicap",this,&Engine::gtpPlaceFreeHandicap);
  gtpe->addFunctionCommand("set_free_handicap",this,&Engine::gtpSetFreeHandicap);
  
  gtpe->addFunctionCommand("param",this,&Engine::gtpParam);
  gtpe->addFunctionCommand("showliberties",this,&Engine::gtpShowLiberties);
  gtpe->addFunctionCommand("showvalidmoves",this,&Engine::gtpShowValidMoves);
  gtpe->addFunctionCommand("showgroupsize",this,&Engine::gtpShowGroupSize);
  gtpe->addFunctionCommand("showpatternmatches",this,&Engine::gtpShowPatternMatches);
  gtpe->addFunctionCommand("loadpatterns",this,&Engine::gtpLoadPatterns);
  gtpe->addFunctionCommand("clearpatterns",this,&Engine::gtpClearPatterns);
  gtpe->addFunctionCommand("doboardcopy",this,&Engine::gtpDoBoardCopy);
  gtpe->addFunctionCommand("featurematchesat",this,&Engine::gtpFeatureMatchesAt);
  gtpe->addFunctionCommand("featureprobdistribution",this,&Engine::gtpFeatureProbDistribution);
  gtpe->addFunctionCommand("listallpatterns",this,&Engine::gtpListAllPatterns);
  gtpe->addFunctionCommand("loadfeaturegammas",this,&Engine::gtpLoadFeatureGammas);
  gtpe->addFunctionCommand("savefeaturegammas",this,&Engine::gtpSaveFeatureGammas);
  gtpe->addFunctionCommand("savefeaturegammasinline",this,&Engine::gtpSaveFeatureGammasInline);
  gtpe->addFunctionCommand("loadcircpatterns",this,&Engine::gtpLoadCircPatterns);
  gtpe->addFunctionCommand("loadcircpatternsnot",this,&Engine::gtpLoadCircPatternsNot);
  gtpe->addFunctionCommand("savecircpatternvalues",this,&Engine::gtpSaveCircPatternValues);
  gtpe->addFunctionCommand("loadcircpatternvalues",this,&Engine::gtpLoadCircPatternValues);
  gtpe->addFunctionCommand("listfeatureids",this,&Engine::gtpListFeatureIds);
  gtpe->addFunctionCommand("showcfgfrom",this,&Engine::gtpShowCFGFrom);
  gtpe->addFunctionCommand("showcircdistfrom",this,&Engine::gtpShowCircDistFrom);
  gtpe->addFunctionCommand("listcircpatternsat",this,&Engine::gtpListCircularPatternsAt);
  gtpe->addFunctionCommand("listcircpatternsatsize",this,&Engine::gtpListCircularPatternsAtSize);
  gtpe->addFunctionCommand("listcircpatternsatsizenot",this,&Engine::gtpListCircularPatternsAtSizeNot);
  gtpe->addFunctionCommand("listallcircularpatterns",this,&Engine::gtpListAllCircularPatterns);
  gtpe->addFunctionCommand("listadjacentgroupsof",this,&Engine::gtpListAdjacentGroupsOf);
  
  gtpe->addFunctionCommand("time_settings",this,&Engine::gtpTimeSettings);
  gtpe->addFunctionCommand("time_left",this,&Engine::gtpTimeLeft);
  
  gtpe->addFunctionCommand("donplayouts",this,&Engine::gtpDoNPlayouts);
  gtpe->addFunctionCommand("outputsgf",this,&Engine::gtpOutputSGF);
  gtpe->addFunctionCommand("playoutsgf",this,&Engine::gtpPlayoutSGF);
  gtpe->addFunctionCommand("playoutsgf_pos",this,&Engine::gtpPlayoutSGF_pos);
  gtpe->addFunctionCommand("gamesgf",this,&Engine::gtpGameSGF);
  
  gtpe->addFunctionCommand("explainlastmove",this,&Engine::gtpExplainLastMove);
  gtpe->addFunctionCommand("boardstats",this,&Engine::gtpBoardStats);
  gtpe->addFunctionCommand("showsymmetrytransforms",this,&Engine::gtpShowSymmetryTransforms);
  gtpe->addFunctionCommand("shownakadecenters",this,&Engine::gtpShowNakadeCenters);
  gtpe->addFunctionCommand("showtreelivegfx",this,&Engine::gtpShowTreeLiveGfx);
  gtpe->addFunctionCommand("describeengine",this,&Engine::gtpDescribeEngine);
  
  gtpe->addFunctionCommand("bookshow",this,&Engine::gtpBookShow);
  gtpe->addFunctionCommand("bookadd",this,&Engine::gtpBookAdd);
  gtpe->addFunctionCommand("bookremove",this,&Engine::gtpBookRemove);
  gtpe->addFunctionCommand("bookclear",this,&Engine::gtpBookClear);
  gtpe->addFunctionCommand("bookload",this,&Engine::gtpBookLoad);
  gtpe->addFunctionCommand("booksave",this,&Engine::gtpBookSave);
  
  gtpe->addFunctionCommand("showcurrenthash",this,&Engine::gtpShowCurrentHash);
  gtpe->addFunctionCommand("showsafepositions",this,&Engine::gtpShowSafePositions);
  gtpe->addFunctionCommand("dobenchmark",this,&Engine::gtpDoBenchmark);
  gtpe->addFunctionCommand("showcriticality",this,&Engine::gtpShowCriticality);
  gtpe->addFunctionCommand("showterritory",this,&Engine::gtpShowTerritory);
  gtpe->addFunctionCommand("showcorrelationmap",this,&Engine::gtpShowCorrelationMap);
  gtpe->addFunctionCommand("showratios",this,&Engine::gtpShowRatios);
  gtpe->addFunctionCommand("showunprune",this,&Engine::gtpShowUnPrune);
  gtpe->addFunctionCommand("showunprunecolor",this,&Engine::gtpShowUnPruneColor);
  gtpe->addFunctionCommand("showraveratios",this,&Engine::gtpShowRAVERatios);
  gtpe->addFunctionCommand("showraveratioscolor",this,&Engine::gtpShowRAVERatiosColor);
  gtpe->addFunctionCommand("showraveratiosother",this,&Engine::gtpShowRAVERatiosOther);

  gtpe->addFunctionCommand("dtload",this,&Engine::gtpDTLoad);
  gtpe->addFunctionCommand("dtclear",this,&Engine::gtpDTClear);
  gtpe->addFunctionCommand("dtprint",this,&Engine::gtpDTPrint);
  gtpe->addFunctionCommand("dtat",this,&Engine::gtpDTAt);
  gtpe->addFunctionCommand("dtupdate",this,&Engine::gtpDTUpdate);
  gtpe->addFunctionCommand("dtsave",this,&Engine::gtpDTSave);
  gtpe->addFunctionCommand("dtset",this,&Engine::gtpDTSet);
  gtpe->addFunctionCommand("dtdistribution",this,&Engine::gtpDTDistribution);
  gtpe->addFunctionCommand("dtstats",this,&Engine::gtpDTStats);
  gtpe->addFunctionCommand("dtpath",this,&Engine::gtpDTPath);
  
  //gtpe->addAnalyzeCommand("final_score","Final Score","string");
  //gtpe->addAnalyzeCommand("showboard","Show Board","string");
  gtpe->addAnalyzeCommand("boardstats","Board Stats","string");
  
  //gtpe->addAnalyzeCommand("showsymmetrytransforms","Show Symmetry Transforms","sboard");
  //gtpe->addAnalyzeCommand("showliberties","Show Liberties","sboard");
  //gtpe->addAnalyzeCommand("showvalidmoves","Show Valid Moves","sboard");
  //gtpe->addAnalyzeCommand("showgroupsize","Show Group Size","sboard");
  gtpe->addAnalyzeCommand("showcfgfrom %%p","Show CFG From","sboard");
  gtpe->addAnalyzeCommand("showcircdistfrom %%p","Show Circular Distance From","sboard");
  gtpe->addAnalyzeCommand("listcircpatternsat %%p","List Circular Patterns At","string");
  gtpe->addAnalyzeCommand("listadjacentgroupsof %%p","List Adjacent Groups Of","string");
  //gtpe->addAnalyzeCommand("showsafepositions","Show Safe Positions","gfx");
  gtpe->addAnalyzeCommand("showpatternmatches","Show Pattern Matches","sboard");
  gtpe->addAnalyzeCommand("showratios","Show Ratios","sboard");
  gtpe->addAnalyzeCommand("showunprune","Show UnpruneFactor","sboard");
  gtpe->addAnalyzeCommand("showunprunecolor","Show UnpruneFactor (color display)","cboard");
  gtpe->addAnalyzeCommand("showraveratios","Show RAVE Ratios","sboard");
  gtpe->addAnalyzeCommand("showraveratioscolor","Show RAVE Ratios (color display)","cboard");
  gtpe->addAnalyzeCommand("showraveratiosother","Show RAVE Ratios (other color)","sboard");
  //gtpe->addAnalyzeCommand("shownakadecenters","Show Nakade Centers","sboard");
  gtpe->addAnalyzeCommand("featurematchesat %%p","Feature Matches At","string");
  gtpe->addAnalyzeCommand("featureprobdistribution","Feature Probability Distribution","cboard");
  gtpe->addAnalyzeCommand("dtdistribution","Decision Tree Distribution","cboard");
  gtpe->addAnalyzeCommand("loadfeaturegammas %%r","Load Feature Gammas","none");
  gtpe->addAnalyzeCommand("showcriticality","Show Criticality","cboard");
  gtpe->addAnalyzeCommand("showterritory","Show Territory","dboard");
  gtpe->addAnalyzeCommand("showcorrelationmap","Show Correlation","dboard");
  gtpe->addAnalyzeCommand("showtreelivegfx","Show Tree Live Gfx","gfx");
  gtpe->addAnalyzeCommand("loadpatterns %%r","Load Patterns","none");
  gtpe->addAnalyzeCommand("clearpatterns","Clear Patterns","none");
  //gtpe->addAnalyzeCommand("doboardcopy","Do Board Copy","none");
  //gtpe->addAnalyzeCommand("showcurrenthash","Show Current Hash","string");
  
  gtpe->addAnalyzeCommand("param general","Parameters (General)","param");
  gtpe->addAnalyzeCommand("param tree","Parameters (Tree)","param");
  gtpe->addAnalyzeCommand("param playout","Parameters (Playout)","param");
  gtpe->addAnalyzeCommand("param time","Parameters (Time)","param");
  gtpe->addAnalyzeCommand("param rules","Parameters (Rules)","param");
  gtpe->addAnalyzeCommand("param other","Parameters (Other)","param");
  gtpe->addAnalyzeCommand("donplayouts %%s","Do N Playouts","none");
  gtpe->addAnalyzeCommand("donplayouts 1","Do 1 Playout","none");
  gtpe->addAnalyzeCommand("donplayouts 100","Do 100 Playouts","none");
  gtpe->addAnalyzeCommand("donplayouts 1000","Do 1k Playouts","none");
  gtpe->addAnalyzeCommand("donplayouts 10000","Do 10k Playouts","none");
  gtpe->addAnalyzeCommand("donplayouts 100000","Do 100k Playouts","none");
  gtpe->addAnalyzeCommand("outputsgf %%w","Output SGF","none");
  gtpe->addAnalyzeCommand("playoutsgf %%w %%c","Playout to SGF (win of color)","none");
  gtpe->addAnalyzeCommand("playoutsgf_pos %%w %%c %%p","Playout to SGF (win of color at pos)","none");
 
  gtpe->addAnalyzeCommand("bookshow","Book Show","gfx");
  gtpe->addAnalyzeCommand("bookadd %%p","Book Add","none");
  gtpe->addAnalyzeCommand("bookremove %%p","Book Remove","none");
  gtpe->addAnalyzeCommand("bookclear","Book Clear","none");
  gtpe->addAnalyzeCommand("bookload %%r","Book Load","none");
  gtpe->addAnalyzeCommand("booksave %%w","Book Save","none");
  
  gtpe->setInterruptFlag(&stopthinking);
  gtpe->setPonderer(&Engine::ponderWrapper,this,&stoppondering);
  gtpe->setWorkerEnabled(params->interrupts_enabled);
  gtpe->setPonderEnabled(params->pondering_enabled);
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
  
  int newsize=cmd->getIntArg(0);
  
  if (newsize<BOARDSIZE_MIN || newsize>BOARDSIZE_MAX)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("unacceptable size");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  me->setBoardSize(newsize);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpClearBoard(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  me->clearBoard();
  
  //assume that this will be the start of a new game
  me->time->resetAll();
  
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
  
  Go::Move move=Go::Move((gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE),vert.x,vert.y,me->boardsize);
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
  me->generateMove((gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE),&move,true);
  Gtp::Vertex vert={move->getX(me->boardsize),move->getY(me->boardsize)};
  delete move;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printVertex(vert);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpGenMoveCleanup(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
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
  
  if (!me->params->cleanup_in_progress)
  {
    me->params->cleanup_in_progress=true;
    if (!me->params->rules_all_stones_alive)
      me->clearMoveTree();
  }
  
  Go::Move *move;
  me->generateMove((gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE),&move,true);
  Gtp::Vertex vert={move->getX(me->boardsize),move->getY(me->boardsize)};
  delete move;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printVertex(vert);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpRegGenMove(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
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
  me->generateMove((gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE),&move,false);
  Gtp::Vertex vert={move->getX(me->boardsize),move->getY(me->boardsize)};
  delete move;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printVertex(vert);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpShowBoard(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  gtpe->getOutput()->printString(me->currentboard->toString());
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpUndo(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  if (me->params->undo_enable && me->undo())
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("cannot undo");
    gtpe->getOutput()->endResponse();
  }
}

float Engine::getScoreKomi() const
{ 
//own test, did not look too bad!!!
//float dynamic_komi=7.5*komi_handicap*exp(-5.0*sqrt(komi_handicap)*(float)currentboard->getMovesMade()/boardsize/boardsize);
//  if (dynamic_komi<5)
//    dynamic_komi=0;  //save the end game
  //Formula Petr Baudis dynamic komi (N=200 for 19x19 board scaled to smaller boards)
  float dynamic_komi=0;
  if (params->dynkomi_enabled)
  {
    dynamic_komi=7.0*komi_handicap*(1-(float)currentboard->getMovesMade()/(boardsize*boardsize*200.0/19.0/19.0));
    if (dynamic_komi<0)
      dynamic_komi=0;  //save the end game
  }
  return komi+komi_handicap+dynamic_komi; 
}

float Engine::getHandiKomi() const
{ 
  return komi+komi_handicap; 
}

void Engine::gtpFinalScore(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  float score;
  
  if (!me->params->rules_all_stones_alive)
  {
    int plts=me->params->rules_all_stones_alive_playouts-me->movetree->getPlayouts();
    if (plts>0)
      me->doNPlayouts(plts);
  }
  
  if (me->params->rules_all_stones_alive || me->params->cleanup_in_progress)
    score=me->currentboard->score()-me->komi-me->komi_handicap;
  else
    score=me->currentboard->territoryScore(me->territorymap,me->params->territory_threshold)-me->komi-me->komi_handicap;
  
  gtpe->getOutput()->startResponse(cmd);
  if (score==0) // jigo
    gtpe->getOutput()->printf("0");
  else
    gtpe->getOutput()->printScore(score);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpFinalStatusList(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("argument is required");
    gtpe->getOutput()->endResponse();
    return;
  }

  std::string arg = cmd->getStringArg(0);
  std::transform(arg.begin(),arg.end(),arg.begin(),::tolower);

  if (!me->params->rules_all_stones_alive)
  {
    int plts=me->params->rules_all_stones_alive_playouts-me->movetree->getPlayouts();
    if (plts>0)
      me->doNPlayouts(plts);
  }
  
  if (arg=="dead")
  {
    if (me->params->rules_all_stones_alive || me->params->cleanup_in_progress)
    {
      gtpe->getOutput()->startResponse(cmd);
      gtpe->getOutput()->endResponse();
    }
    else
    {
      gtpe->getOutput()->startResponse(cmd);
      for (int x=0;x<me->boardsize;x++)
      {
        for (int y=0;y<me->boardsize;y++)
        {
          int pos=Go::Position::xy2pos(x,y,me->boardsize);
          if (me->currentboard->boardData()[pos].color!=Go::EMPTY)
          {
            if (!me->currentboard->isAlive(me->territorymap,me->params->territory_threshold,pos))
            {
              Gtp::Vertex vert={x,y};
              gtpe->getOutput()->printVertex(vert);
              gtpe->getOutput()->printf(" ");
            }
          }
        }
      }
      gtpe->getOutput()->endResponse();
    }
  }
  else if (arg=="alive")
  {
    gtpe->getOutput()->startResponse(cmd);
    for (int x=0;x<me->boardsize;x++)
    {
      for (int y=0;y<me->boardsize;y++)
      {
        int pos=Go::Position::xy2pos(x,y,me->boardsize);
        if (me->currentboard->boardData()[pos].color!=Go::EMPTY)
        {
          if (me->params->rules_all_stones_alive || me->params->cleanup_in_progress || me->currentboard->isAlive(me->territorymap,me->params->territory_threshold,pos))
          {
            Gtp::Vertex vert={x,y};
            gtpe->getOutput()->printVertex(vert);
            gtpe->getOutput()->printf(" ");
          }
        }
      }
    }
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("argument is not supported");
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpShowLiberties(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      if (me->currentboard->boardData()[pos].group==NULL)
        gtpe->getOutput()->printf("\"\" ");
      else
      {
        int lib=me->currentboard->boardData()[pos].group->find()->numOfPseudoLiberties();
        gtpe->getOutput()->printf("\"%d",lib);
        if (me->currentboard->boardData()[pos].group->find()->inAtari())
          gtpe->getOutput()->printf("!");
        gtpe->getOutput()->printf("\" ");
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowValidMoves(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      gtpe->getOutput()->printf("\"");
      if (me->currentboard->validMove(Go::Move(Go::BLACK,Go::Position::xy2pos(x,y,me->boardsize))))
        gtpe->getOutput()->printf("B");
      if (me->currentboard->validMove(Go::Move(Go::WHITE,Go::Position::xy2pos(x,y,me->boardsize))))
        gtpe->getOutput()->printf("W");
      gtpe->getOutput()->printf("\" ");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowGroupSize(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      if (me->currentboard->boardData()[pos].group==NULL)
        gtpe->getOutput()->printf("\"\" ");
      else
      {
        int stones=me->currentboard->boardData()[pos].group->find()->numOfStones();
        gtpe->getOutput()->printf("\"%d\" ",stones);
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowPatternMatches(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      gtpe->getOutput()->printf("\"");
      if (me->currentboard->validMove(Go::Move(Go::BLACK,pos)) && me->patterntable->isPattern(Pattern::ThreeByThree::makeHash(me->currentboard,pos)))
        gtpe->getOutput()->printf("B");
      if (me->currentboard->validMove(Go::Move(Go::WHITE,pos)) && me->patterntable->isPattern(Pattern::ThreeByThree::invert(Pattern::ThreeByThree::makeHash(me->currentboard,pos))))
        gtpe->getOutput()->printf("W");
      gtpe->getOutput()->printf("\" ");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowRatios(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Color col=me->currentboard->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=tree->getRatio();
        gtpe->getOutput()->printf("\"%.2f\"",ratio);
      }
      else
        gtpe->getOutput()->printf("\"\"");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowUnPrune(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Color col=me->currentboard->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=tree->getUnPruneFactor();
        gtpe->getOutput()->printf("\"%.1f\"",ratio);
      }
      else
        gtpe->getOutput()->printf("\"\"");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowRAVERatios(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Color col=me->currentboard->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=tree->getRAVERatio();
        gtpe->getOutput()->printf("\"%.2f\"",ratio);
      }
      else
        gtpe->getOutput()->printf("\"\"");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowRAVERatiosColor(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Color col=me->currentboard->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  float min=1000000;
  float max=0;
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=tree->getRAVERatio();
        if (ratio<min) min=ratio;
        if (ratio>max) max=ratio;
      }
    }
  }
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      float ratio;
      float crit=0;
      if (tree!=NULL)
      {
        ratio=tree->getRAVERatio();
        crit=(ratio-min)/(max-min);
        //fprintf(stderr,"%f %f %f %f\n",min,max,ratio,crit);
      }
      if (crit==0 && (!move.isNormal()))
          gtpe->getOutput()->printf("\"\" ");
      else
      {
          float r,g,b;
          float x=crit;
          
          // scale from blue-red
          r=x;
          if (r>1)
            r=1;
          g=0;
          b=1-r;
          
          if (r<0)
            r=0;
          if (g<0)
            g=0;
          if (b<0)
            b=0;
          gtpe->getOutput()->printf("#%02x%02x%02x ",(int)(r*255),(int)(g*255),(int)(b*255));
          //gtpe->getOutput()->printf("#%06x ",(int)(prob*(1<<24)));
        }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowUnPruneColor(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Color col=me->currentboard->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  float min=1000000;
  float max=-1000000;
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=(tree->getUnPruneFactor());
        if (ratio<min) min=ratio;
        if (ratio>max) max=ratio;
      }
    }
  }
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      float ratio;
      float crit=0;
      if (tree!=NULL)
      {
        ratio=(tree->getUnPruneFactor());
        crit=(ratio-min)/(max-min);
        //fprintf(stderr,"%f %f %f %f\n",min,max,ratio,crit);
      }
      if (crit==0 && (!move.isNormal()))
          gtpe->getOutput()->printf("\"\" ");
      else
      {
          float r,g,b;
          float x=crit;
          
          // scale from blue-red
          r=x;
          if (r>1)
            r=1;
          g=0;
          b=1-r;
          
          if (r<0)
            r=0;
          if (g<0)
            g=0;
          if (b<0)
            b=0;
          gtpe->getOutput()->printf("#%02x%02x%02x ",(int)(r*255),(int)(g*255),(int)(b*255));
          //gtpe->getOutput()->printf("#%06x ",(int)(prob*(1<<24)));
        }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}
        
void Engine::gtpShowRAVERatiosOther(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Color col=me->currentboard->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=tree->getRAVERatioOther();
        gtpe->getOutput()->printf("\"%.2f\"",ratio);
      }
      else
        gtpe->getOutput()->printf("\"\"");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpLoadPatterns(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string patternfile=cmd->getStringArg(0);
  
  delete me->patterntable;
  me->patterntable=new Pattern::ThreeByThreeTable();
  bool success=me->patterntable->loadPatternFile(patternfile);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_LOADPATTERNS);
        me->mpiBroadcastString(patternfile);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded pattern file: %s",patternfile.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error loading pattern file: %s",patternfile.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpClearPatterns(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  delete me->patterntable;
  me->patterntable=new Pattern::ThreeByThreeTable();
  
  #ifdef HAVE_MPI
    if (me->mpirank==0)
      me->mpiBroadcastCommand(MPICMD_CLEARPATTERNS);
  #endif
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDoBoardCopy(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  Go::Board *oldboard=me->currentboard;
  Go::Board *newboard=me->currentboard->copy();
  delete oldboard;
  me->currentboard=newboard;
  if (!me->params->uct_symmetry_use)
    me->currentboard->turnSymmetryOff();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpPlayoutSGF(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;

  if (cmd->numArgs()!=2)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 2 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string sgffile=cmd->getStringArg(0);
  std::string who_wins=cmd->getStringArg(1);

  //can be used to get any win of playout if win=0
  int win=0;
  if (who_wins=="B"||who_wins=="b")
    win=1;
  if (who_wins=="W"||who_wins=="w")
    win=-1;

  bool success=false;
  bool foundwin=false;
  int i;
  float finalscore=0;
  for (i=0;i<1000;i++)
  {
    Go::Board *playoutboard=me->currentboard->copy();
    Go::Color col=me->currentboard->nextToMove();
    std::list<Go::Move> playoutmoves;
    std::list<std::string> movereasons;
    Tree *playouttree = me->movetree->getUrgentChild(me->threadpool->getThreadZero()->getSettings());
    me->playout->doPlayout(me->threadpool->getThreadZero()->getSettings(),playoutboard,finalscore,playouttree,playoutmoves,col,NULL,NULL,NULL,NULL,&movereasons);
    if (finalscore*win>=0)
    {
      foundwin=true;
      success=me->writeSGF(sgffile,me->currentboard,playoutmoves,&movereasons);
      break;
    }
  }

  if (!foundwin)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("No win found for %s",who_wins.c_str());
    gtpe->getOutput()->endResponse();
    return;
  }
  
  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("wrote sgf file: %s (i was %d) finalscore %f",sgffile.c_str(),i,finalscore);
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error writing sgf file: %s",sgffile.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpPlayoutSGF_pos(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;

  if (cmd->numArgs()!=3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 3 arg");
    gtpe->getOutput()->endResponse();
    return;
  }

  std::string sgffile=cmd->getStringArg(0);
  std::string who_wins=cmd->getStringArg(1);
  std::string where_wins=cmd->getStringArg(2);

  int where=Go::Position::string2pos(where_wins,me->boardsize);

  int win=0;
  if (who_wins=="B"||who_wins=="b")
    win=1;
  if (who_wins=="W"||who_wins=="w")
    win=-1;

  bool success=false;
  bool foundwin=false;
  int how_often=0,from_often=0;;
  for (int i=0;i<1000+1000;i++)
  {
    Go::Board *playoutboard=me->currentboard->copy();
    Go::Color col=me->currentboard->nextToMove();
    float finalscore;
    std::list<Go::Move> playoutmoves;
    std::list<std::string> movereasons;
    Tree *playouttree = me->movetree->getUrgentChild(me->threadpool->getThreadZero()->getSettings());
    me->playout->doPlayout(me->threadpool->getThreadZero()->getSettings(),playoutboard,finalscore,playouttree,playoutmoves,col,NULL,NULL,NULL,NULL,&movereasons);
    if (finalscore!=0 && i<1000) from_often++;
    playoutboard->score();
    //fprintf(stderr,"playoutres %d %d finalscore: %f\n",i,playoutboard->getScoredOwner(where),finalscore);
    if ((win==1  && playoutboard->getScoredOwner(where)==Go::BLACK) || (win==-1 && playoutboard->getScoredOwner(where)==Go::WHITE))
    {
      if (i<1000)
        how_often++;
      else
      {
        foundwin=true;
        success=me->writeSGF(sgffile,me->currentboard,playoutmoves,&movereasons);
        break;
      }
    }
  }

  if (!foundwin)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("No win found for %s",who_wins.c_str());
    gtpe->getOutput()->endResponse();
    return;
  }

  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("wrote sgf file: %s  found within the first %d playouts: %d",sgffile.c_str(),from_often,how_often);
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error writing sgf file: %s",sgffile.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpOutputSGF(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string sgffile=cmd->getStringArg(0);
  bool success=me->writeSGF(sgffile,me->currentboard,me->movetree);
  
  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("wrote sgf file: %s",sgffile.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error writing sgf file: %s",sgffile.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpGameSGF(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string sgffile=cmd->getStringArg(0);
  bool success=me->writeGameSGF(sgffile);
  
  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("wrote sgf file: %s",sgffile.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error writing sgf file: %s",sgffile.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpDoNPlayouts(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int n=cmd->getIntArg(0);
  me->doNPlayouts(n);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpExplainLastMove(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  if (me->moveexplanations->size()>0)
    gtpe->getOutput()->printString(me->moveexplanations->back());
  gtpe->getOutput()->endResponse();
}

void Engine::gtpBoardStats(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Board *board=me->currentboard;
  int size=me->boardsize;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("board stats:\n");
  gtpe->getOutput()->printf("komi: %.1f\n",me->komi);
  gtpe->getOutput()->printf("moves: %d\n",board->getMovesMade());
  gtpe->getOutput()->printf("next to move: %c\n",(board->nextToMove()==Go::BLACK?'B':'W'));
  gtpe->getOutput()->printf("passes: %d\n",board->getPassesPlayed());
  gtpe->getOutput()->printf("simple ko: ");
  int simpleko=board->getSimpleKo();
  if (simpleko==-1)
    gtpe->getOutput()->printf("NONE");
  else
  {
    Gtp::Vertex vert={Go::Position::pos2x(simpleko,me->boardsize),Go::Position::pos2y(simpleko,size)};
    gtpe->getOutput()->printVertex(vert);
  }
  gtpe->getOutput()->printf("\n");
  #if SYMMETRY_ONLYDEGRAGE
    gtpe->getOutput()->printf("stored symmetry: %s (degraded)\n",board->getSymmetryString(board->getSymmetry()).c_str());
  #else
    gtpe->getOutput()->printf("stored symmetry: %s\n",board->getSymmetryString(board->getSymmetry()).c_str());
  #endif
  gtpe->getOutput()->printf("computed symmetry: %s\n",board->getSymmetryString(board->computeSymmetry()).c_str());
  for (int p=0;p<board->getPositionMax();p++)
  {
    if (board->inGroup(p) && board->touchingEmpty(p)>0)
    {
      Go::Group *group=board->getGroup(p);
      if (board->isLadder(group))
        gtpe->getOutput()->printf("ladder at %s works: %d\n",Go::Position::pos2string(p,size).c_str(),board->isProbableWorkingLadder(group));
      else
      {
        Go::Color col=Go::otherColor(group->getColor());
        foreach_adjacent(p,q,{
          if (board->onBoard(q))
          {
            Go::Move move=Go::Move(col,q);
            if (board->validMove(move) && board->isLadderAfter(group,move))
              gtpe->getOutput()->printf("ladder at %s after %s works: %d\n",Go::Position::pos2string(p,size).c_str(),move.toString(size).c_str(),board->isProbableWorkingLadderAfter(group,move));
          }
        });
      }
    }
  }
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpFeatureMatchesAt(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Go::Board *board=me->currentboard;
  Go::Color col=board->nextToMove();
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Go::Move move=Go::Move(col,pos);
  
  Go::ObjectBoard<int> *cfglastdist=NULL;
  Go::ObjectBoard<int> *cfgsecondlastdist=NULL;
  me->features->computeCFGDist(board,&cfglastdist,&cfgsecondlastdist);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("Feature Matches for %s:\n",move.toString(board->getSize()).c_str());
  gtpe->getOutput()->printf("PASS:              %u\n",me->features->matchFeatureClass(Features::PASS,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("CAPTURE:           %u\n",me->features->matchFeatureClass(Features::CAPTURE,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("EXTENSION:         %u\n",me->features->matchFeatureClass(Features::EXTENSION,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("SELFATARI:         %u\n",me->features->matchFeatureClass(Features::SELFATARI,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("ATARI:             %u\n",me->features->matchFeatureClass(Features::ATARI,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("BORDERDIST:        %u\n",me->features->matchFeatureClass(Features::BORDERDIST,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("LASTDIST:          %u\n",me->features->matchFeatureClass(Features::LASTDIST,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("SECONDLASTDIST:    %u\n",me->features->matchFeatureClass(Features::SECONDLASTDIST,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("CFGLASTDIST:       %u\n",me->features->matchFeatureClass(Features::CFGLASTDIST,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("CFGSECONDLASTDIST: %u\n",me->features->matchFeatureClass(Features::CFGSECONDLASTDIST,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("PATTERN3X3:        0x%04x\n",me->features->matchFeatureClass(Features::PATTERN3X3,board,cfglastdist,cfgsecondlastdist,move));
  gtpe->getOutput()->printf("CIRCPATT:          %u\n",me->features->matchFeatureClass(Features::CIRCPATT,board,cfglastdist,cfgsecondlastdist,move));
  float gamma=me->features->getMoveGamma(board,cfglastdist,cfgsecondlastdist,move);
  float total=me->features->getBoardGamma(board,cfglastdist,cfgsecondlastdist,col);
  gtpe->getOutput()->printf("Gamma: %.2f/%.2f (%.2f)\n",gamma,total,gamma/total);
  gtpe->getOutput()->endResponse(true);
  
  if (cfglastdist!=NULL)
    delete cfglastdist;
  if (cfgsecondlastdist!=NULL)
    delete cfgsecondlastdist;
}

void Engine::gtpFeatureProbDistribution(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  Go::Board *board=me->currentboard;
  Go::Color col=board->nextToMove();
  
  Go::ObjectBoard<int> *cfglastdist=NULL;
  Go::ObjectBoard<int> *cfgsecondlastdist=NULL;
  me->features->computeCFGDist(board,&cfglastdist,&cfgsecondlastdist);
  
  float totalgamma=me->features->getBoardGamma(board,cfglastdist,cfgsecondlastdist,col);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      Go::Move move=Go::Move(col,Go::Position::xy2pos(x,y,me->boardsize)); 
      float gamma=me->features->getMoveGamma(board,cfglastdist,cfgsecondlastdist,move);
      if (gamma<=0)
        gtpe->getOutput()->printf("\"\" ");
      else
      {
        float prob=gamma/totalgamma;
        float point1=0.15;
        float point2=0.65;
        float r,g,b;
        // scale from blue-green-red-reddest?
        if (prob>=point2)
        {
          b=0;
          r=1;
          g=0;
        }
        else if (prob>=point1)
        {
          b=0;
          r=(prob-point1)/(point2-point1);
          g=1-r;
        }
        else
        {
          r=0;
          g=prob/point1;
          b=1-g;
        }
        if (r<0)
          r=0;
        if (g<0)
          g=0;
        if (b<0)
          b=0;
        gtpe->getOutput()->printf("#%02x%02x%02x ",(int)(r*255),(int)(g*255),(int)(b*255));
        //gtpe->getOutput()->printf("#%06x ",(int)(prob*(1<<24)));
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
  
  if (cfglastdist!=NULL)
    delete cfglastdist;
  if (cfgsecondlastdist!=NULL)
    delete cfgsecondlastdist;
}

void Engine::gtpListAllPatterns(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  Go::Board *board=me->currentboard;
  Go::Color col=board->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  for (int p=0;p<board->getPositionMax();p++)
  {
    if (me->currentboard->validMove(Go::Move(col,p)))
    {
      unsigned int hash=Pattern::ThreeByThree::makeHash(me->currentboard,p);
      if (col==Go::WHITE)
        hash=Pattern::ThreeByThree::invert(hash);
      hash=Pattern::ThreeByThree::smallestEquivalent(hash);
      gtpe->getOutput()->printf("0x%04x ",hash);
    }
  }

  gtpe->getOutput()->endResponse();
}

void Engine::gtpLoadFeatureGammas(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  me->learn_filename_features=filename;
  
  delete me->features;
  me->features=new Features(me->params);
  bool success=me->features->loadGammaFile(filename);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_LOADFEATUREGAMMAS);
        me->mpiBroadcastString(filename);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded features gamma file: %s Attention, circ pattern files are removed by this!",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error loading features gamma file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpSaveFeatureGammas(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  
  bool success=me->features->saveGammaFile(filename);
  
  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("saveded features gamma file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error saveing features gamma file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpSaveFeatureGammasInline(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  
  bool success=me->features->saveGammaFileInline(filename);
  
  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("saveded features gamma file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error saveing features gamma file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpLoadCircPatterns(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=2)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 2 arg (filename and number of lines)");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  int numlines=cmd->getIntArg(1);
  
  if (me->features==NULL)
    me->features=new Features(me->params);
  bool success=me->features->loadCircFile(filename,numlines);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_LOADFEATUREGAMMAS);
        me->mpiBroadcastString(filename);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded circpatterns file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error loading circpatterns file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpLoadCircPatternsNot(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=2)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 2 arg (filename and number of lines)");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  int numlines=cmd->getIntArg(1);
  
  if (me->features==NULL)
    me->features=new Features(me->params);
  bool success=me->features->loadCircFileNot(filename,numlines);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_LOADFEATUREGAMMAS);
        me->mpiBroadcastString(filename);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded circpatterns file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error loading circpatterns file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpSaveCircPatternValues(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg (filename and number of lines)");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  
  if (me->features==NULL)
    me->features=new Features(me->params);
  bool success=me->features->saveCircValueFile(filename);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_LOADFEATUREGAMMAS);
        me->mpiBroadcastString(filename);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("saved circvalue file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error saving circpatterns file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpLoadCircPatternValues(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg (filename and number of lines)");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  me->learn_filename_circ_patterns=filename;
  
  if (me->features==NULL)
    me->features=new Features(me->params);
  bool success=me->features->loadCircValueFile(filename);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_LOADFEATUREGAMMAS);
        me->mpiBroadcastString(filename);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded circvalue file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error saving circpatterns file: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpListFeatureIds(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("\n%s",me->features->getFeatureIdList().c_str());
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowCFGFrom(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Go::ObjectBoard<int> *cfgdist=me->currentboard->getCFGFrom(pos);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int p=Go::Position::xy2pos(x,y,me->boardsize);
      int dist=cfgdist->get(p);
      if (dist!=-1)
        gtpe->getOutput()->printf("\"%d\" ",dist);
      else
        gtpe->getOutput()->printf("\"\" ");
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
  
  delete cfgdist;
}

void Engine::gtpShowCircDistFrom(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int dist=Go::circDist(vert.x,vert.y,x,y);
      gtpe->getOutput()->printf("\"%d\" ",dist);
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpListCircularPatternsAt(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Pattern::Circular pattcirc=Pattern::Circular(me->getCircDict(),me->currentboard,pos,PATTERN_CIRC_MAXSIZE);
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("Circular Patterns at %s:\n",Go::Position::pos2string(pos,me->boardsize).c_str());
  for (int s=2;s<=PATTERN_CIRC_MAXSIZE;s++)
  {
    gtpe->getOutput()->printf(" %s\n",pattcirc.getSubPattern(me->getCircDict(),s).toString(me->getCircDict()).c_str());
  }

  gtpe->getOutput()->printf("Smallest Equivalent:\n");
  pattcirc.convertToSmallestEquivalent(me->getCircDict());
  gtpe->getOutput()->printf(" %s\n",pattcirc.toString(me->getCircDict()).c_str());
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpListCircularPatternsAtSize(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex size and color is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  int s=cmd->getIntArg(1);
  Gtp::Color gtpcol = cmd->getColorArg(2);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Pattern::Circular pattcirc=Pattern::Circular(me->getCircDict(),me->currentboard,pos,PATTERN_CIRC_MAXSIZE);
  if (gtpcol==Gtp::WHITE)
    pattcirc.invert();
  pattcirc.convertToSmallestEquivalent(me->getCircDict());
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf(" %s\n",pattcirc.getSubPattern(me->getCircDict(),s).toString(me->getCircDict()).c_str());
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpListCircularPatternsAtSizeNot(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex size and color is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  int s=cmd->getIntArg(1);
  Gtp::Color gtpcol = cmd->getColorArg(2);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  fprintf(stderr,"played at %s\n",Go::Position::pos2string(pos,me->boardsize).c_str());
  
  Go::BitBoard *validmoves=me->currentboard->getValidMoves(gtpcol==Gtp::BLACK?Go::BLACK:Go::WHITE);
  Random rand;
  rand.makeSeed ();
  int r=rand.getRandomInt(me->currentboard->getPositionMax());
  int d=rand.getRandomInt(1)*2-1;
  for (int p=0;p<me->currentboard->getPositionMax();p++)
  {
    int rp=(r+p*d);
    if (rp<0) rp+=me->currentboard->getPositionMax();
    if (rp>=me->currentboard->getPositionMax()) rp-=me->currentboard->getPositionMax();
    if (pos!=rp && validmoves->get(rp))
    {
      pos=rp;
      break;
    }
  }

  fprintf(stderr,"circ pattern at %s\n",Go::Position::pos2string(pos,me->boardsize).c_str());
          
  Pattern::Circular pattcirc=Pattern::Circular(me->getCircDict(),me->currentboard,pos,PATTERN_CIRC_MAXSIZE);
  if (gtpcol==Gtp::WHITE)
    pattcirc.invert();
  pattcirc.convertToSmallestEquivalent(me->getCircDict());
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf(" %s\n",pattcirc.getSubPattern(me->getCircDict(),s).toString(me->getCircDict()).c_str());
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpListAllCircularPatterns(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  int size=0;
  if (cmd->numArgs()>=1)
  {
    size=cmd->getIntArg(0);
  }
  
  Go::Board *board=me->currentboard;
  Go::Color col=board->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  for (int p=0;p<board->getPositionMax();p++)
  {
    if (me->currentboard->validMove(Go::Move(col,p)))
    {
      Pattern::Circular pattcirc=Pattern::Circular(me->getCircDict(),board,p,PATTERN_CIRC_MAXSIZE);
      if (col==Go::WHITE)
        pattcirc.invert();
      pattcirc.convertToSmallestEquivalent(me->getCircDict());
      if (size==0)
      {
        for (int s=4;s<=PATTERN_CIRC_MAXSIZE;s++)
        {
          gtpe->getOutput()->printf(" %s",pattcirc.getSubPattern(me->getCircDict(),s).toString(me->getCircDict()).c_str());
        }
      }
      else
        gtpe->getOutput()->printf(" %s",pattcirc.getSubPattern(me->getCircDict(),size).toString(me->getCircDict()).c_str());
    }
  }

  gtpe->getOutput()->endResponse();
}

void Engine::gtpListAdjacentGroupsOf(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Go::Group *group=NULL;
  if (me->currentboard->inGroup(pos))
    group=me->currentboard->getGroup(pos);
  
  if (group!=NULL)
  {
    std::list<int,Go::allocator_int> *adjacentgroups=group->getAdjacentGroups();
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("list of size %d:\n",adjacentgroups->size());
    for(std::list<int,Go::allocator_int>::iterator iter=adjacentgroups->begin();iter!=adjacentgroups->end();++iter)
    {
      if (me->currentboard->inGroup((*iter)))
        gtpe->getOutput()->printf("%s\n",Go::Position::pos2string((*iter),me->boardsize).c_str());
    }
    gtpe->getOutput()->endResponse(true);
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpShowSymmetryTransforms(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  Go::Board::Symmetry sym=me->currentboard->getSymmetry();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      int transpos=me->currentboard->doSymmetryTransformToPrimary(sym,pos);
      if (transpos==pos)
        gtpe->getOutput()->printf("\"P\" ");
      else
      {
        Gtp::Vertex vert={Go::Position::pos2x(transpos,me->boardsize),Go::Position::pos2y(transpos,me->boardsize)};
        gtpe->getOutput()->printf("\"");
        gtpe->getOutput()->printVertex(vert);
        gtpe->getOutput()->printf("\" ");
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowNakadeCenters(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      int centerpos=me->currentboard->getThreeEmptyGroupCenterFrom(pos);
      if (centerpos==-1)
        gtpe->getOutput()->printf("\"\" ");
      else
      {
        Gtp::Vertex vert={Go::Position::pos2x(centerpos,me->boardsize),Go::Position::pos2y(centerpos,me->boardsize)};
        gtpe->getOutput()->printf("\"");
        gtpe->getOutput()->printVertex(vert);
        gtpe->getOutput()->printf("\" ");
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowTreeLiveGfx(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  if (me->params->move_policy==Parameters::MP_UCT || me->params->move_policy==Parameters::MP_ONEPLY)
    me->displayPlayoutLiveGfx(-1,false);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDescribeEngine(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("%s\n",me->longname.c_str());
  #ifdef HAVE_MPI
    gtpe->getOutput()->printf("mpi world size: %d\n",me->mpiworldsize);
  #endif
  gtpe->getOutput()->printf("parameters:\n");
  me->params->printParametersForDescription(gtpe);
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowCurrentHash(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("Current hash: 0x%016llx",me->currentboard->getZobristHash(me->zobristtable));
  gtpe->getOutput()->endResponse();
}

void Engine::gtpBookShow(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString(me->book->show(me->boardsize,me->movehistory));
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpBookAdd(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Go::Move move=Go::Move(me->currentboard->nextToMove(),pos);
  
  me->book->add(me->boardsize,me->movehistory,move);
  
  #ifdef HAVE_MPI
    if (me->mpirank==0)
    {
      unsigned int arg=pos;
      me->mpiBroadcastCommand(MPICMD_BOOKADD,&arg);
    }
  #endif
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpBookRemove(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Go::Move move=Go::Move(me->currentboard->nextToMove(),pos);
  
  me->book->remove(me->boardsize,me->movehistory,move);
  
  #ifdef HAVE_MPI
    if (me->mpirank==0)
    {
      unsigned int arg=pos;
      me->mpiBroadcastCommand(MPICMD_BOOKREMOVE,&arg);
    }
  #endif
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpBookClear(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  me->book->clear(me->boardsize);
  
  #ifdef HAVE_MPI
    if (me->mpirank==0)
      me->mpiBroadcastCommand(MPICMD_BOOKCLEAR);
  #endif
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpBookLoad(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  
  delete me->book;
  me->book=new Book(me->params);
  bool success=me->book->loadFile(filename);
  
  if (success)
  {
    #ifdef HAVE_MPI
      if (me->mpirank==0)
      {
        me->mpiBroadcastCommand(MPICMD_BOOKLOAD);
        me->mpiBroadcastString(filename);
      }
    #endif
    
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded book: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error loading book: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpBookSave(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  bool success=me->book->saveFile(filename);
  
  if (success)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("saved book: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error saving book: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpShowSafePositions(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  Go::Board *board=me->currentboard;
  Benson *benson=new Benson(board);
  benson->solve();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("BLACK");
  for (int p=0;p<board->getPositionMax();p++)
  {
    if (benson->getSafePositions()->get(p)==Go::BLACK)
      gtpe->getOutput()->printf(" %s",Go::Position::pos2string(p,me->boardsize).c_str());
  }
  gtpe->getOutput()->printf("\nWHITE");
  for (int p=0;p<board->getPositionMax();p++)
  {
    if (benson->getSafePositions()->get(p)==Go::WHITE)
      gtpe->getOutput()->printf(" %s",Go::Position::pos2string(p,me->boardsize).c_str());
  }
  gtpe->getOutput()->endResponse();
  
  delete benson;
}

void Engine::gtpDoBenchmark(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  int games;
  if (cmd->numArgs()==0)
    games=1000;
  else
    games=cmd->getIntArg(0);
  
  boost::posix_time::ptime time_start=me->timeNow();
  float finalscore;
  for (int i=0;i<games;i++)
  {
    Go::Board *board=new Go::Board(me->boardsize);
    std::list<Go::Move> playoutmoves;
    me->playout->doPlayout(me->threadpool->getThreadZero()->getSettings(),board,finalscore,NULL,playoutmoves,Go::BLACK,NULL,NULL,NULL,NULL);
    delete board;
  }
  
  float rate=(float)games/me->timeSince(time_start)/1000;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("ppms: %.2f",rate);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpShowCriticality(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  Go::Board *board=me->currentboard;
  Go::Color col=board->nextToMove();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  float min=1000000;
  float max=0;
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      Go::Move move=Go::Move(col,pos);
      Tree *tree=me->movetree->getChild(move);
      if (tree!=NULL)
      {
        float ratio=tree->getCriticality();
        if (ratio<min) min=ratio;
        if (ratio>max) max=ratio;
      }
    }
  }
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      Go::Move move=Go::Move(col,Go::Position::xy2pos(x,y,me->boardsize)); 
      Tree *tree=me->movetree->getChild(move);
      if (tree==NULL || !tree->isPrimary())
        gtpe->getOutput()->printf("\"\" ");
      else
      {
        float crit=tree->getCriticality();
        float plts=(me->params->uct_criticality_siblings?me->movetree->getPlayouts():tree->getPlayouts());
        //fprintf(stderr,"%f\n",crit);
        if (crit==0 && (!move.isNormal() || plts==0))
          gtpe->getOutput()->printf("\"\" ");
        else
        {
          crit=(crit-min)/(max-min);
          float r,g,b;
          float x=crit;
          
          // scale from blue-red
          r=x;
          if (r>1)
            r=1;
          g=0;
          b=1-r;
          
          if (r<0)
            r=0;
          if (g<0)
            g=0;
          if (b<0)
            b=0;
          gtpe->getOutput()->printf("#%02x%02x%02x ",(int)(r*255),(int)(g*255),(int)(b*255));
          //gtpe->getOutput()->printf("#%06x ",(int)(prob*(1<<24)));
        }
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

//#define wf(A)   ((A-0.5>0)?(sqrt(2*(A-0.5))+1)/2.0:(1.0-sqrt(-2*(A-0.5)))/2.0)
//#define wf(A)   A
#define wf(A) pow(A,0.5)
void Engine::gtpShowTerritory(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  float territorycount=0;
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      float tmp=me->territorymap->getPositionOwner(pos);
      //if (tmp>0.2) territorycount++;
      //if (tmp<-0.2) territorycount--;
      if (tmp<0)
        territorycount-=wf(-tmp);
      else
        territorycount+=wf(tmp);
      if (tmp<0)
        gtpe->getOutput()->printf("%.2f ",-wf(-tmp));
      else
        gtpe->getOutput()->printf("%.2f ",wf(tmp));  
    }
    gtpe->getOutput()->printf("\n");
  }

  if (territorycount-me->getHandiKomi()>0)
    gtpe->getOutput()->printf("Territory %.1f Komi %.1f B+%.1f (with ScoreKomi %.1f) (%.1f)\n",
      territorycount,me->getHandiKomi(),territorycount-me->getHandiKomi(),territorycount-me->getScoreKomi(),me->getScoreKomi());
  else
    gtpe->getOutput()->printf("Territory %.1f Komi %.1f W+%.1f (with ScoreKomi %.1f) (%.1f)\n",
      territorycount,me->getHandiKomi(),-(territorycount-me->getHandiKomi()),-(territorycount-me->getScoreKomi()),me->getScoreKomi());
    
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpShowCorrelationMap(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      int pos=Go::Position::xy2pos(x,y,me->boardsize);
      float tmp=me->getCorrelation(pos);
      gtpe->getOutput()->printf("%.2f ",tmp);  
    }
    gtpe->getOutput()->printf("\n");
  }
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpParam(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()==0)
  {
    gtpe->getOutput()->startResponse(cmd);
    me->params->printParametersForGTP(gtpe);
    gtpe->getOutput()->endResponse(true);
  }
  else if (cmd->numArgs()==1)
  {
    std::string category=cmd->getStringArg(0);
    
    gtpe->getOutput()->startResponse(cmd);
    me->params->printParametersForGTP(gtpe,category);
    gtpe->getOutput()->endResponse(true);
  }
  else if (cmd->numArgs()==2)
  {
    std::string param=cmd->getStringArg(0);
    std::transform(param.begin(),param.end(),param.begin(),::tolower);

    //these are gammas, they are found due to a : in the string
    //TODO: move this to a separate GTP command
    int pos=param.find(":");
    if (pos>0)
    {
      std::string tmp=cmd->getStringArg(0)+" "+cmd->getStringArg(1);
      //fprintf(stderr,"test %s %d\n",tmp.c_str(),pos);
      if (me->features->loadGammaLine(tmp))
      {
        //fprintf(stderr,"success\n");
        #ifdef HAVE_MPI
          if (me->mpirank==0)
          {
            me->mpiBroadcastCommand(MPICMD_SETPARAM);
            me->mpiBroadcastString(param);
            me->mpiBroadcastString(cmd->getStringArg(1));
          }
        #endif
      
      gtpe->getOutput()->startResponse(cmd);
      gtpe->getOutput()->endResponse();
      return;
      }
    }
    
    if (me->params->setParameter(param,cmd->getStringArg(1)))
    {
      #ifdef HAVE_MPI
        if (me->mpirank==0)
        {
          me->mpiBroadcastCommand(MPICMD_SETPARAM);
          me->mpiBroadcastString(param);
          me->mpiBroadcastString(cmd->getStringArg(1));
        }
      #endif
      
      gtpe->getOutput()->startResponse(cmd);
      gtpe->getOutput()->endResponse();
    }
    else
    {
      gtpe->getOutput()->startResponse(cmd,false);
      gtpe->getOutput()->printf("error setting parameter: %s",param.c_str());
      gtpe->getOutput()->endResponse();
    }
  }
  else if (cmd->numArgs()==3)
  {
    std::string category=cmd->getStringArg(0); //check this parameter in category?
    std::string param=cmd->getStringArg(1);
    std::transform(param.begin(),param.end(),param.begin(),::tolower);
    if (category.compare("feature")==0) //TODO: similar to above, should be moved to a separate GTP command
    {
      std::string tmp=cmd->getStringArg(1)+" "+cmd->getStringArg(2);
      fprintf(stderr,"test %s\n",tmp.c_str());
      me->features->loadGammaLine(tmp);
    }
    if (me->params->setParameter(param,cmd->getStringArg(2)))
    {
      #ifdef HAVE_MPI
        if (me->mpirank==0)
        {
          me->mpiBroadcastCommand(MPICMD_SETPARAM);
          me->mpiBroadcastString(param);
          me->mpiBroadcastString(cmd->getStringArg(2));
        }
      #endif
      
      gtpe->getOutput()->startResponse(cmd);
      gtpe->getOutput()->endResponse();
    }
    else
    {
      gtpe->getOutput()->startResponse(cmd,false);
      gtpe->getOutput()->printf("error setting parameter: %s",param.c_str());
      gtpe->getOutput()->endResponse();
    }
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 0 to 3 args");
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpTimeSettings(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid arguments");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  delete me->time;
  me->time=new Time(me->params,cmd->getIntArg(0),cmd->getIntArg(1),cmd->getIntArg(2));
  
  #ifdef HAVE_MPI
    if (me->mpirank==0)
    {
      unsigned int arg1=cmd->getIntArg(0);
      unsigned int arg2=cmd->getIntArg(1);
      unsigned int arg3=cmd->getIntArg(2);
      me->mpiBroadcastCommand(MPICMD_TIMESETTINGS,&arg1,&arg2,&arg3);
    }
  #endif
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpTimeLeft(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (me->time->isNoTiming())
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("no time settings defined");
    gtpe->getOutput()->endResponse();
    return;
  }
  else if (cmd->numArgs()!=3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid arguments");
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
  Go::Color col=(gtpcol==Gtp::BLACK ? Go::BLACK : Go::WHITE);
  
  float oldtime=me->time->timeLeft(col);
  //int oldstones=me->time->stonesLeft(col);
  float newtime=(float)cmd->getIntArg(1);
  int newstones=cmd->getIntArg(2);
  
  if (newstones==0)
    gtpe->getOutput()->printfDebug("[time_left]: diff:%.3f\n",newtime-oldtime);
  else
    gtpe->getOutput()->printfDebug("[time_left]: diff:%.3f s:%d\n",newtime-oldtime,newstones);
  
  me->time->updateTimeLeft(col,newtime,newstones);
  
  #ifdef HAVE_MPI
    if (me->mpirank==0)
    {
      unsigned int arg1=(unsigned int)col;
      unsigned int arg2=(unsigned int)newtime;
      unsigned int arg3=newstones;
      me->mpiBroadcastCommand(MPICMD_TIMELEFT,&arg1,&arg2,&arg3);
    }
  #endif
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpChat(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()<3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("missing arguments");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  bool pm = (cmd->getStringArg(0)=="private");
  std::string name = cmd->getStringArg(1);
  std::string msg = cmd->getStringArg(2);
  for (unsigned int i=3;i<cmd->numArgs();i++)
  {
    msg+=" "+cmd->getStringArg(i);
  }

  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf(me->chat(pm,name,msg));
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDTLoad(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  
  std::list<DecisionTree*> *trees = DecisionTree::loadFile(me->params,filename);
  if (trees!=NULL)
  {
    for (std::list<DecisionTree*>::iterator iter=trees->begin();iter!=trees->end();++iter)
    {
      me->decisiontrees.push_back((*iter));
    }
    delete trees;

    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("loaded decision trees: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error loading decision trees: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpDTSave(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()<1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("need 1 arg");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  std::string filename=cmd->getStringArg(0);
  bool ignorestats = false;
  if (cmd->numArgs()>1)
    ignorestats = cmd->getIntArg(1)!=0;
  
  bool res = DecisionTree::saveFile(&(me->decisiontrees),filename,ignorestats);
  if (res)
  {
    gtpe->getOutput()->startResponse(cmd);
    gtpe->getOutput()->printf("saved decision trees: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
  else
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printf("error saving decision trees: %s",filename.c_str());
    gtpe->getOutput()->endResponse();
  }
}

void Engine::gtpDTClear(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  for (std::list<DecisionTree*>::iterator iter=me->decisiontrees.begin();iter!=me->decisiontrees.end();++iter)
  {
    delete (*iter);
  }
  me->decisiontrees.clear();
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("cleared decision trees");
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDTPrint(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;

  bool ignorestats = false;
  if (cmd->numArgs()>=1)
    ignorestats = cmd->getIntArg(0)!=0;
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("decision trees:\n");
  int leafoffset = 0;
  for (std::list<DecisionTree*>::iterator iter=me->decisiontrees.begin();iter!=me->decisiontrees.end();++iter)
  {
    gtpe->getOutput()->printf("%s\n",(*iter)->toString(ignorestats,leafoffset).c_str());
    leafoffset += (*iter)->getLeafCount();
  }
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpDTAt(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("vertex is required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  Gtp::Vertex vert = cmd->getVertexArg(0);
  
  if (vert.x==-3 && vert.y==-3)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("invalid vertex");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int pos=Go::Position::xy2pos(vert.x,vert.y,me->boardsize);
  Go::Move move=Go::Move(me->currentboard->nextToMove(),pos);

  float w = DecisionTree::getCollectionWeight(&(me->decisiontrees), me->currentboard, move);

  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("weight for %s: %.2f",move.toString(me->boardsize).c_str(),w);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDTUpdate(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  DecisionTree::collectionUpdateDescent(&(me->decisiontrees),me->currentboard);
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("updated decision trees");
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDTSet(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=2)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("id and weight are required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int id = cmd->getIntArg(0);
  float weight = cmd->getFloatArg(1);
  
  DecisionTree::setCollectionLeafWeight(&(me->decisiontrees), id, weight);

  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("decision tree leaf weight updated");
  gtpe->getOutput()->endResponse();
}

void Engine::gtpDTDistribution(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  Go::Board *board = me->currentboard;
  Go::Color col = board->nextToMove();

  float weightmax = 0;
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      Go::Move move = Go::Move(col,Go::Position::xy2pos(x,y,me->boardsize)); 
      float weight = DecisionTree::getCollectionWeight(&(me->decisiontrees), board, move);
      if (weight >= 0 && weight > weightmax)
        weightmax = weight;
    }
  }
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printString("\n");
  for (int y=me->boardsize-1;y>=0;y--)
  {
    for (int x=0;x<me->boardsize;x++)
    {
      Go::Move move = Go::Move(col,Go::Position::xy2pos(x,y,me->boardsize)); 
      float weight = DecisionTree::getCollectionWeight(&(me->decisiontrees), board, move);
      if (weight < 0)
        gtpe->getOutput()->printf("\"\" ");
      else
      {
        //float val = atan(weight)/asin(1);
        float val = weight/weightmax;
        float point1 = 0.15;
        float point2 = 0.65;
        float r,g,b;
        // scale from blue-green-red-reddest?
        if (val >= point2)
        {
          b = 0;
          r = 1;
          g = 0;
        }
        else if (val >= point1)
        {
          b = 0;
          r = (val-point1)/(point2-point1);
          g = 1 - r;
        }
        else
        {
          r = 0;
          g = val/point1;
          b = 1 - g;
        }
        if (r < 0)
          r = 0;
        if (g < 0)
          g = 0;
        if (b < 0)
          b = 0;
        gtpe->getOutput()->printf("#%02x%02x%02x ",(int)(r*255),(int)(g*255),(int)(b*255));
        //gtpe->getOutput()->printf("#%06x ",(int)(prob*(1<<24)));
      }
    }
    gtpe->getOutput()->printf("\n");
  }

  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpDTStats(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;

  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("decision trees stats:");
  for (std::list<DecisionTree*>::iterator iter=me->decisiontrees.begin();iter!=me->decisiontrees.end();++iter)
  {
    std::vector<std::string> *attrs = (*iter)->getAttrs();
    std::string attrstr = "";
    for (unsigned int i=0;i<attrs->size();i++)
    {
      if (i!=0)
        attrstr += "|";
      attrstr += attrs->at(i);
    }

    int treenodes;
    int leaves;
    int maxdepth;
    float avgdepth;
    int maxnodes;
    float avgnodes;
    (*iter)->getTreeStats(treenodes,leaves,maxdepth,avgdepth,maxnodes,avgnodes);

    gtpe->getOutput()->printf("\nStats for DT[%s]:\n",attrstr.c_str());
    gtpe->getOutput()->printf("  Nodes: %d\n",treenodes);
    gtpe->getOutput()->printf("  Leaves: %d\n",leaves);
    gtpe->getOutput()->printf("  Max Depth: %d\n",maxdepth);
    gtpe->getOutput()->printf("  Avg Depth: %.2f\n",avgdepth);
    gtpe->getOutput()->printf("  Max Nodes: %d\n",maxnodes);
    gtpe->getOutput()->printf("  Avg Nodes: %.2f\n",avgnodes);
  }
  gtpe->getOutput()->endResponse(true);
}

void Engine::gtpDTPath(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("leaf id required");
    gtpe->getOutput()->endResponse();
    return;
  }
  
  int id = cmd->getIntArg(0);
  
  std::string path = DecisionTree::getCollectionLeafPath(&(me->decisiontrees), id);

  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->printf("decision tree leaf %d path: %s",id,path.c_str());
  gtpe->getOutput()->endResponse();
}

void Engine::gtpGameOver(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;

  me->gameFinished();
  
  gtpe->getOutput()->startResponse(cmd);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpEcho(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  gtpe->getOutput()->startResponse(cmd);
  for (unsigned int i=0; i<cmd->numArgs(); i++)
  {
    if (i!=0)
      gtpe->getOutput()->printf(" ");
    gtpe->getOutput()->printf(cmd->getStringArg(i));
  }
  gtpe->getOutput()->endResponse();
}

void Engine::gtpPlaceFreeHandicap(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;
  
  if (cmd->numArgs()!=1)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("argument is required");
    gtpe->getOutput()->endResponse();
    return;
  }

  int numHandicapstones=cmd->getIntArg (0);
  if (numHandicapstones<2||numHandicapstones>9)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("number of handicap stones not supported");
    gtpe->getOutput()->endResponse();
    return;
  }

  int sizem1=me->boardsize-1;
  int borderdist=3;
  if (me->boardsize<13)
    borderdist=2;
  int sizemb=sizem1-borderdist;
  Gtp::Vertex vert[9];
  vert[0].x=borderdist;   vert[0].y=borderdist;
  vert[1].x=sizemb;       vert[1].y=sizemb;
  vert[2].x=sizemb;       vert[2].y=borderdist;
  vert[3].x=borderdist;   vert[3].y=sizemb;
  vert[4].x=borderdist;   vert[4].y=sizem1/2;
  vert[5].x=sizemb;       vert[5].y=sizem1/2;
  vert[6].x=sizem1/2;     vert[6].y=borderdist;
  vert[7].x=sizem1/2;     vert[7].y=sizemb;
  vert[8].x=sizem1/2;     vert[8].y=sizem1/2;
  if (numHandicapstones>4 && numHandicapstones%2==1)
  {
    vert[numHandicapstones-1].x=sizem1/2; vert[numHandicapstones-1].y=sizem1/2;
  }
  gtpe->getOutput()->startResponse(cmd);
  for (int i=0;i<numHandicapstones;i++)
  {
    Go::Move move=Go::Move(Go::BLACK,vert[i].x,vert[i].y,me->boardsize);
    me->makeMove(move);
    gtpe->getOutput()->printVertex(vert[i]);
    gtpe->getOutput()->printf(" ");
  }
  me->setHandicapKomi(numHandicapstones);
  gtpe->getOutput()->endResponse();
}

void Engine::gtpSetFreeHandicap(void *instance, Gtp::Engine* gtpe, Gtp::Command* cmd)
{
  Engine *me=(Engine*)instance;

  int numVertices=cmd->numArgs();
  if (cmd->numArgs()<2)
  {
    gtpe->getOutput()->startResponse(cmd,false);
    gtpe->getOutput()->printString("At least 2 handicap stones required");
    gtpe->getOutput()->endResponse();
    return;
  }

  gtpe->getOutput()->startResponse(cmd);
  for (int x=0;x<numVertices;x++)
  {
    Gtp::Vertex vert=cmd->getVertexArg (x);
    Go::Move move=Go::Move(Go::BLACK,vert.x,vert.y,me->boardsize);
    if (!me->isMoveAllowed(move))
    {
      gtpe->getOutput()->startResponse(cmd,false);
      gtpe->getOutput()->printString("illegal move");
      gtpe->getOutput()->endResponse();
      return;
    }
    me->makeMove(move);
  
    //gtpe->getOutput()->printVertex(vert);
    //gtpe->getOutput()->printf(" ");
  }
  me->setHandicapKomi(numVertices);
  gtpe->getOutput()->endResponse();
}

void Engine::learnFromTree(Go::Board *tmpboard, Tree *learntree, std::ostringstream *ssun, int movenum)
{
  int num_unpruned=learntree->getNumUnprunedChildren();
  std::map<float,Go::Move,std::greater<float> > ordervalue;
  std::map<float,Tree*,std::greater<float> > orderlearntree;
  std::map<float,Go::Move,std::greater<float> > ordergamma;

  float forcesort=0;
  *ssun<<"\nun:"<<movenum<<"(";
  Go::ObjectBoard<int> *cfglastdist=NULL;
  Go::ObjectBoard<int> *cfgsecondlastdist=NULL;
  getFeatures()->computeCFGDist(tmpboard,&cfglastdist,&cfgsecondlastdist);
  for (int nn=1;nn<=num_unpruned;nn++)
  {
    for(std::list<Tree*>::iterator iter=learntree->getChildren()->begin();iter!=learntree->getChildren()->end();++iter) 
    {
      if ((*iter)->getUnprunedNum()==nn && (*iter)->isPrimary() && !(*iter)->isPruned())
      {
        *ssun<<(nn!=1?",":"")<<Go::Position::pos2string((*iter)->getMove().getPosition(),boardsize);
        //do not use getFeatureGamma of the tree, as this might be not exactly the order of the gammas to be trained
        ordergamma.insert(std::make_pair(getFeatures()->getMoveGamma(tmpboard,cfglastdist,cfgsecondlastdist,(*iter)->getMove())+forcesort,(*iter)->getMove()));
        ordervalue.insert(std::make_pair((*iter)->getPlayouts()+forcesort,(*iter)->getMove()));
        orderlearntree.insert(std::make_pair((*iter)->getPlayouts()+forcesort,(*iter)));
        forcesort+=0.001012321232123;
      }
    }
  }
  //include pruned into learning, as they all lost!!
  for(std::list<Tree*>::iterator iter=learntree->getChildren()->begin();iter!=learntree->getChildren()->end();++iter) 
  {
    if ((*iter)->isPrimary() && (*iter)->isPruned())
    {
      //ssun<<(nn!=1?",":"")<<Go::Position::pos2string((*iter)->getMove().getPosition(),boardsize);
      //do not use getFeatureGamma of the tree, as this might be not exactly the order of the gammas to be trained
      ordergamma.insert(std::make_pair(getFeatures()->getMoveGamma(tmpboard,cfglastdist,cfgsecondlastdist,(*iter)->getMove())+forcesort,(*iter)->getMove()));
      ordervalue.insert(std::make_pair(0.0+forcesort,(*iter)->getMove()));
      forcesort+=0.001012321232123;
    }
  }
  *ssun<<")";
  if (ordergamma.size()!=ordervalue.size())
    *ssun<<"\nthe ordering of gamma versus mc did not work correctly "<<ordergamma.size()<<" "<<ordervalue.size()<<"\n";
  //*ssun<<" ordermc:(";

  //for the moves (getPosition) the difference mc_position - gamma_position is calculated into numvalue_gamma
  std::map<int,int> mc_pos_move;
  std::map<int,int> gamma_move_pos;
  std::map<int,float> numvalue_gamma;
  std::map<int,float> move_gamma;
  float sum_gammas=0;
  std::map<float,Go::Move>::iterator it;
  int nn=1;
  for (it=ordervalue.begin();it!=ordervalue.end();++it)
  {
    //*ssun<<(nn!=1?",":"")<<Go::Position::pos2string(it->second.getPosition(),boardsize);
    mc_pos_move.insert(std::make_pair(nn,it->second.getPosition()));
    nn++;
  }
  //*ssun<<") ordergamma:(";
  nn=1;
#define sign(A) ((A>0)?1:((A<0)?-1:0))
#define gamma_from_mc_position(A) (move_gamma.find(mc_pos_move.find(A)->second)->second)
  for (it=ordergamma.begin();it!=ordergamma.end();++it)
  {
    //*ssun<<(nn!=1?",":"")<<Go::Position::pos2string(it->second.getPosition(),boardsize);
    gamma_move_pos.insert(std::make_pair(it->second.getPosition(),nn));
    move_gamma.insert(std::make_pair(it->second.getPosition(),it->first));
    sum_gammas+=it->first;
    nn++;
  }
  getFeatures()->learnMovesGamma(tmpboard,cfglastdist,cfgsecondlastdist,ordervalue,move_gamma,sum_gammas);
  *ssun<<")";
  std::map<float,Tree*>::iterator it_learntree;
  for (it_learntree=orderlearntree.begin();it_learntree!=orderlearntree.end();++it_learntree)
  {
    //check if enough playouts
    if (params->mm_learn_min_playouts>=it_learntree->second->getPlayouts ())
      break;
    //tmpboard must be copied
    Go::Board *nextboard=tmpboard->copy();
    //the move must be made first!
    nextboard->makeMove(it_learntree->second->getMove());
    *ssun<<"-"<<it_learntree->second->getMove().toString (boardsize)<<"-";
    //learnFromTree has to be called
    learnFromTree (nextboard,it_learntree->second,ssun,movenum+1);
  }
}

void Engine::generateMove(Go::Color col, Go::Move **move, bool playmove)
{
  clearStatistics();

  if (params->book_use)
  {
    std::list<Go::Move> bookmoves=book->getMoves(boardsize,movehistory);
    
    if (bookmoves.size()>0)
    {
      int r=threadpool->getThreadZero()->getSettings()->rand->getRandomInt(bookmoves.size());
      int i=0;
      for (std::list<Go::Move>::iterator iter=bookmoves.begin();iter!=bookmoves.end();++iter)
      {
        if (i==r)
        {
          *move=new Go::Move(*iter);
          
          if (playmove)
            this->makeMove(**move);
            
          std::string lastexplanation="selected move from book";
          if (playmove)
            moveexplanations->back()=lastexplanation;
          
          gtpe->getOutput()->printfDebug("[genmove]: %s\n",lastexplanation.c_str());
          if (params->livegfx_on)
            gtpe->getOutput()->printfDebug("gogui-gfx: TEXT [genmove]: %s\n",lastexplanation.c_str());
          
          return;
        }
        i++;
      }
    }
  }
  if (params->move_policy==Parameters::MP_UCT || params->move_policy==Parameters::MP_ONEPLY)
  {
    boost::posix_time::ptime time_start=this->timeNow();
    int totalplayouts=0;
    float time_allocated;
    float playouts_per_milli;
    
    params->early_stop_occured=false;
    stopthinking=false;
    
    if (!time->isNoTiming())
    {
      if (params->time_ignore)
      {
        time_allocated=0;
        gtpe->getOutput()->printfDebug("[time_allowed]: ignoring time settings!\n");
      }
      else
      {
        time_allocated=time->getAllocatedTimeForNextTurn(col);
        gtpe->getOutput()->printfDebug("[time_allowed]: %.3f\n",time_allocated);
      }
    }
    else
      time_allocated=0;
    
    if (params->livegfx_on)
      gtpe->getOutput()->printfDebug("gogui-gfx: TEXT [genmove]: starting...\n");
    
    Go::Color expectedcol=currentboard->nextToMove();
    currentboard->setNextToMove(col);
    
    if (expectedcol!=col)
    {
      gtpe->getOutput()->printfDebug("WARNING! Unexpected color. Discarding tree.\n");
      this->clearMoveTree();
    }
    
    #ifdef HAVE_MPI
      MPIRANK0_ONLY(
        unsigned int tmp1=(unsigned int)col;
        this->mpiBroadcastCommand(MPICMD_GENMOVE,&tmp1);
      );
    #endif
    
    movetree->pruneSuperkoViolations();
    this->allowContinuedPlay();
    this->updateTerritoryScoringInTree();
    params->uct_slow_update_last=0;
    params->uct_last_r2=-1;
    
    int startplayouts=(int)movetree->getPlayouts();
    #ifdef HAVE_MPI
      params->mpi_last_update=MPI::Wtime();
    #endif
    
    params->uct_initial_playouts=startplayouts;
    params->thread_job=Parameters::TJ_GENMOVE;
    threadpool->startAll();
    threadpool->waitAll();
    
    totalplayouts=(int)movetree->getPlayouts()-startplayouts;
    //fprintf(stderr,"tplts: %d\n",totalplayouts);
    
    if (movetree->isTerminalResult())
      gtpe->getOutput()->printfDebug("SOLVED! found 100%% sure result after %d plts!\n",totalplayouts);

    int num_unpruned=movetree->getNumUnprunedChildren();
    std::ostringstream ssun;
    if (params->mm_learn_enabled)
      learnFromTree (currentboard,movetree,&ssun,1);
    ssun<<"st:(";
    for (int nn=0;nn<STATISTICS_NUM;nn++)
    {
      ssun<<(nn!=0?",":"");
      ssun<<getStatistics (nn);
    }
    ssun<<")";
    ssun<< " ravepreset: " << (presetplayouts/presetnum);
    Tree *besttree=movetree->getRobustChild();
    float scoresd=0;
    float scoremean=0;
    float bestratio=0;
    int   best_unpruned=0;
    float ratiodelta=-1;
    bool bestsame=false;
    if (besttree==NULL)
    {
      fprintf(stderr,"WARNING! No move found!\n");
      *move=new Go::Move(col,Go::Move::RESIGN);
    }
    else if (!besttree->isTerminalWin() && besttree->getRatio()<params->resign_ratio_threshold && currentboard->getMovesMade()>(params->resign_move_factor_threshold*boardsize*boardsize))
    {
      *move=new Go::Move(col,Go::Move::RESIGN);
      bestratio=besttree->getRatio();
    }
    else
    {
      *move=new Go::Move(col,besttree->getMove().getPosition());
      bestratio=besttree->getRatio();
      scoresd=besttree->getScoreSD();
      scoremean=besttree->getScoreMean();
      best_unpruned=besttree->getUnprunedNum();
      
      ratiodelta=besttree->bestChildRatioDiff();
      bestsame=(besttree==(movetree->getBestRatioChild(10)));
    }
    
    if (params->uct_slow_update_last!=0)
      this->doSlowUpdate();
    
    /*if ((**move).isResign())
    {
      gtpe->getOutput()->printfDebug("[resign]: %s\n",besttree!=NULL?besttree->getMove().toString(boardsize).c_str():"NONE");
      this->writeSGF("debugresign.sgf",currentboard,movetree);
    }*/
    
    if (playmove)
      this->makeMove(**move);
    
    if (params->livegfx_on)
      gtpe->getOutput()->printfDebug("gogui-gfx: CLEAR\n");
    
    float time_used=this->timeSince(time_start);
    //fprintf(stderr,"tu: %f\n",time_used);
    if (time_used>0)
      playouts_per_milli=(float)totalplayouts/(time_used*1000);
    else
      playouts_per_milli=-1;
    if (!time->isNoTiming())
      time->useTime(col,time_used);
    
    std::ostringstream ss;
    ss << std::fixed;
    ss << "r:"<<std::setprecision(3)<<bestratio;
    if (!time->isNoTiming())
    {
      ss << " tl:"<<std::setprecision(3)<<time->timeLeft(col);
      if (time->stonesLeft(col)>0)
        ss << " s:"<<time->stonesLeft(col);
    }
    //this was added because of a strange bug crashing some times in the following lines
    //I did not really found the problem?!
    //fprintf(stderr,"debug %f\n",scoresd);
    if (!time->isNoTiming() || params->early_stop_occured)
      ss << " plts:"<<totalplayouts;
    ss << " ppms:"<<std::setprecision(2)<<playouts_per_milli;
    ss << " rd:"<<std::setprecision(3)<<ratiodelta;
    ss << " r2:"<<std::setprecision(2)<<params->uct_last_r2;
    ss << " fs:"<<std::setprecision(2)<<scoremean;
    ss << " fsd:"<<std::setprecision(2)<<scoresd;
    ss << " un:"<<best_unpruned<<"/"<<num_unpruned;
    ss << " bs:"<<bestsame;
    
    Tree *pvtree=movetree->getRobustChild(true);
    if (pvtree!=NULL)
    {
      std::list<Go::Move> pvmoves=pvtree->getMovesFromRoot();
      ss<<" pv:(";
      for(std::list<Go::Move>::iterator iter=pvmoves.begin();iter!=pvmoves.end();++iter) 
      {
        ss<<(iter!=pvmoves.begin()?",":"")<<Go::Position::pos2string((*iter).getPosition(),boardsize);
      }
      ss<<")";
    }

    ss << " " << ssun.str();

    if (params->surewin_expected)
      ss << " surewin!";
    std::string lastexplanation=ss.str();

    if (playmove)
      moveexplanations->back()=lastexplanation;
    
    gtpe->getOutput()->printfDebug("[genmove]: %s\n",lastexplanation.c_str());
    if (params->livegfx_on)
      gtpe->getOutput()->printfDebug("gogui-gfx: TEXT [genmove]: %s\n",lastexplanation.c_str());
  }
  else
  {
    *move=new Go::Move(col,Go::Move::PASS);
    Go::Board *playoutboard=currentboard->copy();
    playoutboard->turnSymmetryOff();
    if (params->playout_features_enabled)
      playoutboard->setFeatures(features,params->playout_features_incremental);
    playout->getPlayoutMove(threadpool->getThreadZero()->getSettings(),playoutboard,col,**move,NULL);
    if (params->playout_useless_move)
      playout->checkUselessMove(threadpool->getThreadZero()->getSettings(),playoutboard,col,**move,(std::string *)NULL);
    delete playoutboard;
    this->makeMove(**move);
  }
}

void Engine::getOnePlayoutMove(Go::Board *board, Go::Color col, Go::Move *move)
{
  Go::Board *playoutboard=board->copy();
  playoutboard->turnSymmetryOff();
  if (params->playout_features_enabled)
    playoutboard->setFeatures(features,params->playout_features_incremental);
  playout->getPlayoutMove(threadpool->getThreadZero()->getSettings(),playoutboard,col,*move,NULL);
  if (params->playout_useless_move)
    playout->checkUselessMove(threadpool->getThreadZero()->getSettings(),playoutboard,col,*move,(std::string *)NULL);
  delete playoutboard;
}

bool Engine::isMoveAllowed(Go::Move move)
{
  return currentboard->validMove(move);
}

void Engine::makeMove(Go::Move move)
{
  #ifdef HAVE_MPI
    MPIRANK0_ONLY(
      unsigned int tmp1=(unsigned int)move.getColor();
      unsigned int tmp2=(unsigned int)move.getPosition();
      this->mpiBroadcastCommand(MPICMD_MAKEMOVE,&tmp1,&tmp2);
    );
  #endif
#define WITH_P(A) (A>=1.0 || (A>0 && threadpool->getThreadZero()->getSettings()->rand->getRandomReal()<A))  
  if (WITH_P(params->features_output_competitions))
  {
    bool isawinner=true;
    Go::ObjectBoard<int> *cfglastdist=NULL;
    Go::ObjectBoard<int> *cfgsecondlastdist=NULL;
    features->computeCFGDist(currentboard,&cfglastdist,&cfgsecondlastdist);
    
    if (params->features_output_competitions_mmstyle)
    {
      int p=move.getPosition();
      std::string featurestring=features->getMatchingFeaturesString(currentboard,cfglastdist,cfgsecondlastdist,move,!params->features_output_competitions_mmstyle);
      if (featurestring.length()>0)
      {
        gtpe->getOutput()->printfDebug("[features]:# competition (%d,%s)\n",(currentboard->getMovesMade()+1),Go::Position::pos2string(move.getPosition(),boardsize).c_str());
        gtpe->getOutput()->printfDebug("[features]:%s*",Go::Position::pos2string(p,boardsize).c_str());
        gtpe->getOutput()->printfDebug("%s",featurestring.c_str());
        gtpe->getOutput()->printfDebug("\n");
      }
      else
        isawinner=false; 
    }
    else
      gtpe->getOutput()->printfDebug("[features]:# competition (%d,%s)\n",(currentboard->getMovesMade()+1),Go::Position::pos2string(move.getPosition(),boardsize).c_str());
    
    if (isawinner)
    {
      Go::Color col=move.getColor();
      for (int p=0;p<currentboard->getPositionMax();p++)
      {
        Go::Move m=Go::Move(col,p);
        if (currentboard->validMove(m) || m==move)
        {
          std::string featurestring=features->getMatchingFeaturesString(currentboard,cfglastdist,cfgsecondlastdist,m,!params->features_output_competitions_mmstyle);
          if (featurestring.length()>0)
          {
            gtpe->getOutput()->printfDebug("[features]:%s",Go::Position::pos2string(p,boardsize).c_str());
            if (m==move)
              gtpe->getOutput()->printfDebug("*");
            else
              gtpe->getOutput()->printfDebug(":");
            gtpe->getOutput()->printfDebug("%s",featurestring.c_str());
            gtpe->getOutput()->printfDebug("\n");
          }
        }
      }
      {
        Go::Move m=Go::Move(col,Go::Move::PASS);
        if (currentboard->validMove(m) || m==move)
        {
          gtpe->getOutput()->printfDebug("[features]:PASS");
          if (m==move)
            gtpe->getOutput()->printfDebug("*");
          else
            gtpe->getOutput()->printfDebug(":");
          gtpe->getOutput()->printfDebug("%s",features->getMatchingFeaturesString(currentboard,cfglastdist,cfgsecondlastdist,m,!params->features_output_competitions_mmstyle).c_str());
          gtpe->getOutput()->printfDebug("\n");
        }
      }
    }
    
    if (cfglastdist!=NULL)
      delete cfglastdist;
    if (cfgsecondlastdist!=NULL)
      delete cfgsecondlastdist;
  }

  if (WITH_P(params->features_circ_list))
  {
    Go::Color col=currentboard->nextToMove();
    
    for (int p=0;p<currentboard->getPositionMax();p++)
    {
      if (currentboard->validMove(Go::Move(col,p)))
      {
        Pattern::Circular pattcirc=Pattern::Circular(this->getCircDict(),currentboard,p,PATTERN_CIRC_MAXSIZE);
        if (col==Go::WHITE)
          pattcirc.invert();
        pattcirc.convertToSmallestEquivalent(this->getCircDict());
        if (params->features_circ_list_size==0)
        {
          for (int s=3;s<=PATTERN_CIRC_MAXSIZE;s++)
          {
            gtpe->getOutput()->printfDebug("%s",pattcirc.getSubPattern(this->getCircDict(),s).toString(this->getCircDict()).c_str());
            if (s==PATTERN_CIRC_MAXSIZE)
              gtpe->getOutput()->printfDebug("\n");
            else
              gtpe->getOutput()->printfDebug(" ");
          }
        }
        else
        {
          bool found = false;
          for (int s=PATTERN_CIRC_MAXSIZE;s>params->features_circ_list_size;s--)
          {
            Pattern::Circular pc = pattcirc.getSubPattern(this->getCircDict(),s);
            if (features->hasCircPattern(&pc))
            {
              found = true;
              break;
            }
          }
          if (!found)
          {
            Pattern::Circular pc = pattcirc.getSubPattern(this->getCircDict(),params->features_circ_list_size);
            gtpe->getOutput()->printfDebug("%s\n",pc.toString(this->getCircDict()).c_str());
          }
        }
      }
    }
    }

  if (params->dt_update_prob > 0)
  {
    for (std::list<DecisionTree*>::iterator iter=decisiontrees.begin();iter!=decisiontrees.end();++iter)
    {
      if (WITH_P(params->dt_update_prob))
        (*iter)->updateDescent(currentboard);
    }
  }

  if (WITH_P(params->dt_output_mm))
  {
    if (move.isNormal())
    {
      std::list<int> *ids = DecisionTree::getCollectionLeafIds(&decisiontrees,currentboard,move);
      if (ids != NULL)
      {
        gtpe->getOutput()->printfDebug("[dt]:#\n");
        std::string idstring = "";
        for (std::list<int>::iterator iter=ids->begin();iter!=ids->end();++iter)
        {
          idstring += (iter==ids->begin()?"":" ") + boost::lexical_cast<std::string>((*iter));
        }
        if (idstring.size() > 0)
          gtpe->getOutput()->printfDebug("[dt]:%s\n",idstring.c_str());
        delete ids;

        Go::Color col=move.getColor();
        for (int p=0;p<currentboard->getPositionMax();p++)
        {
          Go::Move m=Go::Move(col,p);
          if (currentboard->validMove(m) || m==move)
          {
            std::list<int> *ids = DecisionTree::getCollectionLeafIds(&decisiontrees,currentboard,m);
            if (ids != NULL)
            {
              std::string idstring = "";
              for (std::list<int>::iterator iter=ids->begin();iter!=ids->end();++iter)
              {
                idstring += (iter==ids->begin()?"":" ") + boost::lexical_cast<std::string>((*iter));
              }
              if (idstring.size() > 0)
                gtpe->getOutput()->printfDebug("[dt]:%s\n",idstring.c_str());
              delete ids;
            }
          }
        }
      }
    }
  }
  
  if (params->features_ordered_comparison)
  {
    bool usedpos[currentboard->getPositionMax()+1];
    for (int i=0;i<=currentboard->getPositionMax();i++)
      usedpos[i]=false;
    int posused=0;
    float bestgamma=-1;
    int bestpos=0;
    Go::Color col=move.getColor();
    int matchedat=0;
    
    Go::ObjectBoard<int> *cfglastdist=NULL;
    Go::ObjectBoard<int> *cfgsecondlastdist=NULL;
    features->computeCFGDist(currentboard,&cfglastdist,&cfgsecondlastdist);

    float weights[currentboard->getPositionMax()];
    for (int p=0;p<currentboard->getPositionMax();p++)
    {
      Go::Move m = Go::Move(col,p);
      if (currentboard->validMove(m) || m==move)
        weights[p] = features->getMoveGamma(currentboard,cfglastdist,cfgsecondlastdist,m);
      else
        weights[p] = -1;
    }
  
    gtpe->getOutput()->printfDebug("[feature_comparison]:# comparison (%d,%s)\n",(currentboard->getMovesMade()+1),Go::Position::pos2string(move.getPosition(),boardsize).c_str());
    
    gtpe->getOutput()->printfDebug("[feature_comparison]:");
    while (true)
    {
      for (int p=0;p<currentboard->getPositionMax();p++)
      {
        if (!usedpos[p])
        {
          Go::Move m=Go::Move(col,p);
          if (currentboard->validMove(m) || m==move)
          {
            //float gamma=features->getMoveGamma(currentboard,cfglastdist,cfgsecondlastdist,m);
            float gamma = weights[p];
            if (gamma>bestgamma)
            {
              bestgamma=gamma;
              bestpos=p;
            }
          }
        }
      }
      
      {
        int p=currentboard->getPositionMax();
        if (!usedpos[p])
        {
          Go::Move m=Go::Move(col,Go::Move::PASS);
          if (currentboard->validMove(m) || m==move)
          {
            float gamma=features->getMoveGamma(currentboard,cfglastdist,cfgsecondlastdist,m);;
            if (gamma>bestgamma)
            {
              bestgamma=gamma;
              bestpos=p;
            }
          }
        }
      }
      
      if (bestgamma!=-1)
      {
        Go::Move m;
        if (bestpos==currentboard->getPositionMax())
          m=Go::Move(col,Go::Move::PASS);
        else
          m=Go::Move(col,bestpos);
        posused++;
        usedpos[bestpos]=true;
        gtpe->getOutput()->printfDebug(" %s",Go::Position::pos2string(m.getPosition(),boardsize).c_str());
        if (m==move)
        {
          gtpe->getOutput()->printfDebug("*");
          matchedat=posused;
        }
      }
      else
        break;
      
      bestgamma=-1;
    }
    gtpe->getOutput()->printfDebug("\n");
    gtpe->getOutput()->printfDebug("[feature_comparison]:matched at: %d\n",matchedat);
    
    if (cfglastdist!=NULL)
      delete cfglastdist;
    if (cfgsecondlastdist!=NULL)
      delete cfgsecondlastdist;
  }

  if (params->dt_ordered_comparison)
  {
    bool usedpos[currentboard->getPositionMax()];
    for (int i=0;i<currentboard->getPositionMax();i++)
      usedpos[i] = false;
    int posused = 0;
    float bestweight = -1;
    int bestpos = 0;
    Go::Color col = move.getColor();
    int matchedat = 0;

    float weights[currentboard->getPositionMax()];
    for (int p=0;p<currentboard->getPositionMax();p++)
    {
      Go::Move m = Go::Move(col,p);
      if (currentboard->validMove(m) || m==move)
        weights[p] = DecisionTree::getCollectionWeight(&decisiontrees,currentboard,m);
      else
        weights[p] = -1;
    }
    
    gtpe->getOutput()->printfDebug("[dt_comparison]:# comparison (%d,%s)\n",(currentboard->getMovesMade()+1),Go::Position::pos2string(move.getPosition(),boardsize).c_str());
    
    gtpe->getOutput()->printfDebug("[dt_comparison]:");
    while (true)
    {
      for (int p=0;p<currentboard->getPositionMax();p++)
      {
        if (!usedpos[p])
        {
          Go::Move m = Go::Move(col,p);
          if (currentboard->validMove(m) || m==move)
          {
            float weight = weights[p];
            if (weight > bestweight)
            {
              bestweight = weight;
              bestpos = p;
            }
          }
        }
      }
      
      if (bestweight!=-1)
      {
        Go::Move m = Go::Move(col,bestpos);
        posused++;
        usedpos[bestpos]=true;
        gtpe->getOutput()->printfDebug(" %s",Go::Position::pos2string(m.getPosition(),boardsize).c_str());
        if (m==move)
        {
          gtpe->getOutput()->printfDebug("*");
          matchedat = posused;
        }
      }
      else
        break;
      
      bestweight = -1;
    }
    gtpe->getOutput()->printfDebug("\n");
    gtpe->getOutput()->printfDebug("[dt_comparison]:matched at: %d\n",matchedat);
  }
  
  currentboard->makeMove(move);
  movehistory->push_back(move);
  moveexplanations->push_back("");
  Go::ZobristHash hash=currentboard->getZobristHash(zobristtable);
  if (move.isNormal() && hashtree->hasHash(hash))
    gtpe->getOutput()->printfDebug("WARNING! move is a superko violation\n");
  hashtree->addHash(hash);
  params->uct_slow_update_last=0;
  params->uct_slow_debug_last=0;
  territorymap->decay(params->territory_decayfactor);
  //was memory leak
  //blackOldMoves=new float[currentboard->getPositionMax()];
  //whiteOldMoves=new float[currentboard->getPositionMax()];
  for (int i=0;i<currentboard->getPositionMax();i++)
  {
    blackOldMoves[i]=0;
    whiteOldMoves[i]=0;
  }
  blackOldMean=0.5;
  whiteOldMean=0.5;
  blackOldMovesNum=0;
  whiteOldMovesNum=0;

  if (params->uct_keep_subtree)
    this->chooseSubTree(move);
  else
    this->clearMoveTree();
  #ifdef HAVE_MPI
    mpihashtable.clear();
  #endif

  isgamefinished=false;
  if (currentboard->getPassesPlayed()>=2 || move.isResign())
    this->gameFinished();
}

void Engine::setBoardSize(int s)
{
  if (s<BOARDSIZE_MIN || s>BOARDSIZE_MAX)
    return;
  
  #ifdef HAVE_MPI
    MPIRANK0_ONLY(
      unsigned int tmp=(unsigned int)s;
      this->mpiBroadcastCommand(MPICMD_SETBOARDSIZE,&tmp);
    );
  #endif
  
  boardsize=s;
  params->board_size=boardsize;
  this->clearBoard();
}

void Engine::setKomi(float k)
{
  #ifdef HAVE_MPI
    MPIRANK0_ONLY(
      unsigned int tmp=(unsigned int)k;
      this->mpiBroadcastCommand(MPICMD_SETKOMI,&tmp);
    );
  #endif
  komi=k;
}

void Engine::clearBoard()
{
  this->gameFinished();
  #ifdef HAVE_MPI
    MPIRANK0_ONLY(this->mpiBroadcastCommand(MPICMD_CLEARBOARD););
  #endif
  bool newsize=(zobristtable->getSize()!=boardsize);
  delete currentboard;
  delete movehistory;
  delete moveexplanations;
  delete hashtree;
  delete territorymap;
  delete correlationmap;
  delete blackOldMoves;
  delete whiteOldMoves;
  
  if (newsize)
    delete zobristtable;
  currentboard = new Go::Board(boardsize);
  movehistory = new std::list<Go::Move>();
  moveexplanations = new std::list<std::string>();
  hashtree=new Go::ZobristTree();
  territorymap=new Go::TerritoryMap(boardsize);
  correlationmap=new Go::ObjectBoard<Go::CorrelationData>(boardsize);
  blackOldMoves=new float[currentboard->getPositionMax()];
  whiteOldMoves=new float[currentboard->getPositionMax()];
  for (int i=0;i<currentboard->getPositionMax();i++)
  {
    blackOldMoves[i]=0;
    whiteOldMoves[i]=0;
  }
  blackOldMean=0.5;
  whiteOldMean=0.5;
  blackOldMovesNum=0;
  whiteOldMovesNum=0;

  if (newsize)
    zobristtable=new Go::ZobristTable(params,boardsize,ZOBRIST_HASH_SEED);
  if (!params->uct_symmetry_use)
    currentboard->turnSymmetryOff();
  this->clearMoveTree();
  params->surewin_expected=false;
  playout->resetLGRF();
  params->cleanup_in_progress=false;
  isgamefinished=false;
  komi_handicap=0;
}

void Engine::clearMoveTree()
{
  #ifdef HAVE_MPI
    MPIRANK0_ONLY(this->mpiBroadcastCommand(MPICMD_CLEARTREE););
  #endif
  
  if (movetree!=NULL)
    delete movetree;
  
  if (currentboard->getMovesMade()>0)
    movetree=new Tree(params,currentboard->getZobristHash(zobristtable),currentboard->getLastMove());
  else
    movetree=new Tree(params,0);
  
  params->uct_slow_update_last=0;
  params->uct_slow_debug_last=0;
  params->tree_instances=0; // reset as lock free implementation could be slightly off
}

bool Engine::undo()
{
  if (currentboard->getMovesMade()<=0)
    return false;

  std::list<Go::Move> oldhistory = *movehistory;
  oldhistory.pop_back();
  this->clearBoard();

  for(std::list<Go::Move>::iterator iter=oldhistory.begin();iter!=oldhistory.end();++iter)
  {
    this->makeMove((*iter));
  }

  return true;
}

void Engine::chooseSubTree(Go::Move move)
{
  Tree *subtree=movetree->getChild(move);
  
  if (subtree==NULL)
  {
    //gtpe->getOutput()->printfDebug("no such subtree...\n");
    this->clearMoveTree();
    return;
  }
  
  if (!subtree->isPrimary())
  {
    //gtpe->getOutput()->printfDebug("doing transformation...\n");
    subtree->performSymmetryTransformParentPrimary();
    subtree=movetree->getChild(move);
    if (subtree==NULL || !subtree->isPrimary())
      gtpe->getOutput()->printfDebug("WARNING! symmetry transformation failed! (null:%d)\n",(subtree==NULL));
  }
  
  if (subtree==NULL) // only true if a symmetry transform failed
  {
    gtpe->getOutput()->printfDebug("WARNING! clearing tree...\n");
    this->clearMoveTree();
    return;
  }
  
  movetree->divorceChild(subtree);

  //keep the childrens values
  std::list<Tree*>* childtmp=movetree->getChildren();
  float sum=0;
  int num=0;
  std::list<Tree*>::iterator iter=childtmp->begin();
  Go::Color col=((*iter)->getMove()).getColor();
  for (int i=0;i<currentboard->getPositionMax();i++)
  {
    if (col==Go::BLACK)
      blackOldMoves[i]=0;
    else
      whiteOldMoves[i]=0;
  }
  for(iter=childtmp->begin();iter!=childtmp->end();++iter) 
    {
      if (!(*iter)->isPruned() && ((*iter)->getMove()).isNormal())
      {
        num++;
        sum+=(*iter)->getRatio();
        if (col==Go::BLACK)
        {
          blackOldMoves[((*iter)->getMove()).getPosition()]=(*iter)->getRatio();
          //fprintf(stderr,"blackOldMoves %s %f (%f) ((%f))\n",((*iter)->getMove()).toString(boardsize).c_str(),(*iter)->getRatio(),(*iter)->getUrgency(),(*iter)->getRatio()-(*iter)->getUrgency());
         }
        else
        {
          whiteOldMoves[((*iter)->getMove()).getPosition()]=(*iter)->getRatio();
          //fprintf(stderr,"whiteOldMoves %s %f (%f) ((%f))\n",((*iter)->getMove()).toString(boardsize).c_str(),(*iter)->getRatio(),(*iter)->getUrgency(),(*iter)->getRatio()-(*iter)->getUrgency());
        }
      }
    }
  if (col==Go::BLACK && num>0)
  {
    blackOldMean=sum/num;
    blackOldMovesNum=movetree->getPlayouts();
    //fprintf(stderr,"blackOldMean %f\n",blackOldMean);
  }
  if (col==Go::WHITE && num>0)
  {
    whiteOldMean=sum/num;
    whiteOldMovesNum=movetree->getPlayouts();
    //fprintf(stderr,"whiteOldMean %f\n",whiteOldMean);
  }
  delete movetree;
  movetree=subtree;
  movetree->pruneSuperkoViolations();
}

bool Engine::writeSGF(std::string filename, Go::Board *board, Tree *tree)
{
  std::ofstream sgffile;
  sgffile.open(filename.c_str());
  sgffile<<"(;\nFF[4]SZ["<<boardsize<<"]KM["<<komi<<"]\n";
  if (board==NULL)
    board=currentboard;
  sgffile<<board->toSGFString()<<"\n";
  if (tree==NULL)
    tree=movetree;
  sgffile<<tree->toSGFString()<<"\n)";
  sgffile.close();
  
  return true;
}

bool Engine::writeSGF(std::string filename, Go::Board *board, std::list<Go::Move> playoutmoves, std::list<std::string> *movereasons)
{
  std::ofstream sgffile;
  sgffile.open(filename.c_str());
  sgffile<<"(;\nFF[4]SZ["<<boardsize<<"]KM["<<komi<<"]\n";
  if (board==NULL)
    board=currentboard;
  sgffile<<board->toSGFString()<<"\n";

  std::list<std::string>::iterator reasoniter;
  if (movereasons!=NULL)
    reasoniter=movereasons->begin();
  for(std::list<Go::Move>::iterator iter=playoutmoves.begin();iter!=playoutmoves.end();++iter)
  {
    //sgffile<<tree->toSGFString()<<"\n)";
    sgffile<<";"<<Go::colorToChar((*iter).getColor())<<"[";
    if (!(*iter).isPass()&&!(*iter).isResign())
    {
      sgffile<<(char)((*iter).getX(params->board_size)+'a');
      sgffile<<(char)(params->board_size-(*iter).getY(params->board_size)+'a'-1);
    }
    else if ((*iter).isPass())
      sgffile<<"pass";
    sgffile<<"]";
    if (movereasons!=NULL)
      sgffile<<"C["<<(*reasoniter)<<"]";
    sgffile<<"\n";
    if (movereasons!=NULL)
    {
      ++reasoniter;
      if (reasoniter==movereasons->end())
        break;
    }
  }
  sgffile <<")\n";
  sgffile.close();
  return true;
}

bool Engine::writeGameSGF(std::string filename)
{
  std::ofstream sgffile;
  sgffile.open(filename.c_str());
  sgffile<<"(;\nFF[4]SZ["<<boardsize<<"]KM["<<komi<<"]\n";

  std::list<std::string>::iterator expiter = moveexplanations->begin();
  for(std::list<Go::Move>::iterator iter=movehistory->begin();iter!=movehistory->end();++iter)
  {
    sgffile<<";"<<Go::colorToChar((*iter).getColor())<<"[";
    if (!(*iter).isPass()&&!(*iter).isResign())
    {
      sgffile<<(char)((*iter).getX(params->board_size)+'a');
      sgffile<<(char)(params->board_size-(*iter).getY(params->board_size)+'a'-1);
    }
    else if ((*iter).isPass())
      sgffile<<"pass";
    sgffile<<"]C["<<(*expiter)<<"]\n";

    ++expiter;
    if (expiter==moveexplanations->end())
      break;
  }
  sgffile <<")\n";
  sgffile.close();
  return true;
}

void Engine::doNPlayouts(int n)
{
  if (params->move_policy==Parameters::MP_UCT || params->move_policy==Parameters::MP_ONEPLY)
  {
    stopthinking=false;
    
    this->allowContinuedPlay();
    
    int oldplts=params->playouts_per_move;
    params->playouts_per_move=n;
    
    params->uct_initial_playouts=(int)movetree->getPlayouts();
    params->thread_job=Parameters::TJ_DONPLTS;
    threadpool->startAll();
    threadpool->waitAll();
    if (movetree->isTerminalResult())
      gtpe->getOutput()->printfDebug("SOLVED! found 100%% sure result after %d plts!\n",(int)movetree->getPlayouts()-params->uct_initial_playouts);
    
    params->playouts_per_move=oldplts;
    
    if (params->livegfx_on)
      gtpe->getOutput()->printfDebug("gogui-gfx: CLEAR\n");
  }
}

void Engine::doPlayout(Worker::Settings *settings, Go::BitBoard *firstlist, Go::BitBoard *secondlist, Go::BitBoard *earlyfirstlist, Go::BitBoard *earlysecondlist)
{
  //bool givenfirstlist,givensecondlist;
  Go::Color col=currentboard->nextToMove();
  
  if (movetree->isLeaf())
  {
    this->allowContinuedPlay();
    movetree->expandLeaf(settings);
    movetree->pruneSuperkoViolations();
  }
  
  //givenfirstlist=(firstlist==NULL);
  //givensecondlist=(secondlist==NULL);
  
  Tree *playouttree = movetree->getUrgentChild(settings);
  if (playouttree==NULL)
  {
    if (params->debug_on)
      gtpe->getOutput()->printfDebug("WARNING! No playout target found.\n");
    return;
  }
  std::list<Go::Move> playoutmoves=playouttree->getMovesFromRoot();
  if (playoutmoves.size()==0)
  {
    if (params->debug_on)
      gtpe->getOutput()->printfDebug("WARNING! Bad playout target found.\n");
    return;
  }
  
  //if (!givenfirstlist)
  //  firstlist=new Go::BitBoard(boardsize);
  //if (!givensecondlist)
  //  secondlist=new Go::BitBoard(boardsize);
  
  Go::Board *playoutboard=currentboard->copy();
  playoutboard->turnSymmetryOff();
  if (params->playout_features_enabled)
    playoutboard->setFeatures(features,params->playout_features_incremental);
  if (params->rave_moves>0)
  {
    firstlist->clear();
    secondlist->clear();
    earlyfirstlist->clear();
    earlysecondlist->clear();
  }
  Go::Color playoutcol=playoutmoves.back().getColor();
  
  float finalscore;
  playout->doPlayout(settings,playoutboard,finalscore,playouttree,playoutmoves,col,(params->rave_moves>0?firstlist:NULL),(params->rave_moves>0?secondlist:NULL),(params->rave_moves>0?earlyfirstlist:NULL),(params->rave_moves>0?earlysecondlist:NULL));
  if (this->getTreeMemoryUsage()>(params->memory_usage_max*1024*1024) && !stopthinking)
  {
      gtpe->getOutput()->printfDebug("WARNING! Memory limit reached! Stopping search right now!\n");
      this->stopThinking();
  }
  if (!params->rules_all_stones_alive && !params->cleanup_in_progress && playoutboard->getPassesPlayed()>=2 && (playoutboard->getMovesMade()-currentboard->getMovesMade())<=2)
  {
    finalscore=playoutboard->territoryScore(territorymap,params->territory_threshold)-params->engine->getHandiKomi();
  }
  
  bool playoutwin=Go::Board::isWinForColor(playoutcol,finalscore);
  bool playoutjigo=(finalscore==0);
  if (playoutjigo)
    playouttree->addPartialResult(0.5,1,false);
  else if (playoutwin)
    playouttree->addWin(finalscore);
  else
    playouttree->addLose(finalscore);
  
  playoutboard->updateTerritoryMap(territorymap);

  //here with with firstlist and secondlist the correlationmap can be updated
  if (col==Go::BLACK)
    playoutboard->updateCorrelationMap(correlationmap,firstlist,secondlist);
  else
    playoutboard->updateCorrelationMap(correlationmap,secondlist,firstlist);

  if (!playoutjigo)
  {
    Go::Color wincol=(finalscore>0?Go::BLACK:Go::WHITE);
    playouttree->updateCriticality(playoutboard,wincol);
  }
  
  if (!playouttree->isTerminalResult())
  {
    if (params->uct_points_bonus!=0)
    {
      float scorediff=(playoutcol==Go::BLACK?1:-1)*finalscore;
      //float bonus=params->uct_points_bonus*scorediff;
      float bonus;
      if (scorediff>0)
        bonus=params->uct_points_bonus*log(scorediff+1);
      else
        bonus=-params->uct_points_bonus*log(-scorediff+1);
      playouttree->addPartialResult(bonus,0);
      //fprintf(stderr,"[points_bonus]: %+6.1f %+f\n",scorediff,bonus);
    }
    if (params->uct_length_bonus!=0)
    {
      int moves=playoutboard->getMovesMade();
      float bonus=(playoutwin?1:-1)*params->uct_length_bonus*log(moves);
      playouttree->addPartialResult(bonus,0);
      //fprintf(stderr,"[length_bonus]: %6d %+f\n",moves,bonus);
    }
  }
  
  if (params->debug_on)
  {
    if (finalscore==0)
      gtpe->getOutput()->printfDebug("[result]:jigo\n");
    else if (playoutwin && playoutcol==col)
      gtpe->getOutput()->printfDebug("[result]:win (fs:%+.1f)\n",finalscore);
    else
      gtpe->getOutput()->printfDebug("[result]:lose (fs:%+.1f)\n",finalscore);
  }
  
  if (params->rave_moves>0)
  {
    if (!playoutjigo) // ignore jigos for RAVE
    {
      bool blackwin=Go::Board::isWinForColor(Go::BLACK,finalscore);
      Go::Color wincol=(blackwin?Go::BLACK:Go::WHITE);
      
      if (col==Go::BLACK)
        playouttree->updateRAVE(wincol,firstlist,secondlist,false);
      else
        playouttree->updateRAVE(wincol,secondlist,firstlist,false);
      if (col==Go::BLACK)
        playouttree->updateRAVE(wincol,earlyfirstlist,earlysecondlist,true);
      else
        playouttree->updateRAVE(wincol,earlysecondlist,earlyfirstlist,true);
    }
  }
  
  if (params->uct_virtual_loss)
    playouttree->removeVirtualLoss();
  
  if (settings->thread->getID()==0)
  {
    params->uct_slow_update_last++;
    if (params->uct_slow_update_last>=params->uct_slow_update_interval)
    {
      params->uct_slow_update_last=0;
      
      this->doSlowUpdate();
    }
    if (params->uct_slow_debug_interval>0)
    {
      params->uct_slow_debug_last++;
      if (params->uct_slow_debug_last>=params->uct_slow_debug_interval)
      {
        params->uct_slow_debug_last=0;
        
        if (!movetree->isLeaf())
        {
          std::ostringstream ss;
          ss << std::fixed;
          ss << "[dbg|" << std::setprecision(0)<<movetree->getPlayouts() << "]";
          Tree *robustmove=movetree->getRobustChild();
          ss << " (rm:" << Go::Position::pos2string(robustmove->getMove().getPosition(),boardsize);
          ss << " r:" << std::setprecision(2)<<robustmove->getRatio();
          ss << " r2:" << std::setprecision(2)<<robustmove->secondBestPlayoutRatio();
          ss << ")";
          Tree *bestratio=movetree->getBestRatioChild();
          if (bestratio!=NULL)
          {
            if (robustmove==bestratio)
              ss << " (same)";
            else
            {
              ss << " (br:" << Go::Position::pos2string(bestratio->getMove().getPosition(),boardsize);
              ss << " r:" << std::setprecision(2)<<bestratio->getRatio();
              ss << ")";
            }
          }
          ss << "\n";
          gtpe->getOutput()->printfDebug(ss.str());
        }
      }
    }
  }
  
  delete playoutboard;
  
  //if (!givenfirstlist)
  //  delete firstlist;
  //if (!givensecondlist)
  //  delete secondlist;
}

void Engine::displayPlayoutLiveGfx(int totalplayouts, bool livegfx)
{
  Go::Color col=currentboard->nextToMove();
  
  if (livegfx)
    gtpe->getOutput()->printfDebug("gogui-gfx:\n");
  if (totalplayouts!=-1)
    gtpe->getOutput()->printfDebug("TEXT [genmove]: thinking... playouts:%d\n",totalplayouts);
  
  if (livegfx)
    gtpe->getOutput()->printfDebug("INFLUENCE");
  else
    gtpe->getOutput()->printf("INFLUENCE");
  int maxplayouts=1; //prevent div by zero
  for(std::list<Tree*>::iterator iter=movetree->getChildren()->begin();iter!=movetree->getChildren()->end();++iter) 
  {
    if ((*iter)->getPlayouts()>maxplayouts)
      maxplayouts=(int)(*iter)->getPlayouts();
  }
  float colorfactor=(col==Go::BLACK?1:-1);
  for(std::list<Tree*>::iterator iter=movetree->getChildren()->begin();iter!=movetree->getChildren()->end();++iter) 
  {
    if (!(*iter)->getMove().isPass() && !(*iter)->getMove().isResign())
    {
      Gtp::Vertex vert={(*iter)->getMove().getX(boardsize),(*iter)->getMove().getY(boardsize)};
      float playoutpercentage=(float)(*iter)->getPlayouts()/maxplayouts;
      if (playoutpercentage>1)
        playoutpercentage=1;
      
      if (livegfx)
      {
        gtpe->getOutput()->printfDebug(" ");
        gtpe->getOutput()->printDebugVertex(vert);
        gtpe->getOutput()->printfDebug(" %.2f",playoutpercentage*colorfactor);
      }
      else
      {
        gtpe->getOutput()->printf(" ");
        gtpe->getOutput()->printVertex(vert);
        gtpe->getOutput()->printf(" %.2f",playoutpercentage*colorfactor);
      }
    }
  }
  if (livegfx)
    gtpe->getOutput()->printfDebug("\n");
  else
    gtpe->getOutput()->printf("\n");
  if (params->move_policy==Parameters::MP_UCT)
  {
    if (livegfx)
      gtpe->getOutput()->printfDebug("VAR");
    else
      gtpe->getOutput()->printf("VAR");
    Tree *besttree=movetree->getRobustChild(true);
    if (besttree!=NULL)
    {
      std::list<Go::Move> bestmoves=besttree->getMovesFromRoot();
      for(std::list<Go::Move>::iterator iter=bestmoves.begin();iter!=bestmoves.end();++iter) 
      {
        if (!(*iter).isPass() && !(*iter).isResign())
        {
          Gtp::Vertex vert={(*iter).getX(boardsize),(*iter).getY(boardsize)};
          if (livegfx)
          {
            gtpe->getOutput()->printfDebug(" %c ",((*iter).getColor()==Go::BLACK?'B':'W'));
            gtpe->getOutput()->printDebugVertex(vert);
          }
          else
          {
            gtpe->getOutput()->printf(" %c ",((*iter).getColor()==Go::BLACK?'B':'W'));
            gtpe->getOutput()->printVertex(vert);
          }
        }
        else if ((*iter).isPass())
        {
          if (livegfx)
            gtpe->getOutput()->printfDebug(" %c PASS",((*iter).getColor()==Go::BLACK?'B':'W'));
          else
            gtpe->getOutput()->printf(" %c PASS",((*iter).getColor()==Go::BLACK?'B':'W'));
        }
      }
    }
  }
  else
  {
    if (livegfx)
      gtpe->getOutput()->printfDebug("SQUARE");
    else
      gtpe->getOutput()->printf("SQUARE");
    for(std::list<Tree*>::iterator iter=movetree->getChildren()->begin();iter!=movetree->getChildren()->end();++iter) 
    {
      if (!(*iter)->getMove().isPass() && !(*iter)->getMove().isResign())
      {
        if ((*iter)->getPlayouts()==maxplayouts)
        {
          Gtp::Vertex vert={(*iter)->getMove().getX(boardsize),(*iter)->getMove().getY(boardsize)};
          if (livegfx)
          {
            gtpe->getOutput()->printfDebug(" ");
            gtpe->getOutput()->printDebugVertex(vert);
          }
          else
          {
            gtpe->getOutput()->printf(" ");
            gtpe->getOutput()->printVertex(vert);
          }
        }
      }
    }
  }
  if (livegfx)
    gtpe->getOutput()->printfDebug("\n\n");
}

void Engine::allowContinuedPlay()
{
  if (currentboard->getPassesPlayed()>=2)
  {
    currentboard->resetPassesPlayed();
    movetree->allowContinuedPlay();
    gtpe->getOutput()->printfDebug("WARNING! continuing play from a terminal position\n");
  }
}

void Engine::ponder()
{
  if (!(params->pondering_enabled) || (currentboard->getMovesMade()<=0) || (currentboard->getPassesPlayed()>=2) || (currentboard->getLastMove().isResign()) || (book->getMoves(boardsize,movehistory).size()>0))
    return;
  
  if (params->move_policy==Parameters::MP_UCT || params->move_policy==Parameters::MP_ONEPLY)
  {
    if (this->getTreeMemoryUsage()>(params->memory_usage_max*1024*1024))
      return;

    //fprintf(stderr,"pondering starting!\n");
    this->allowContinuedPlay();
    params->uct_slow_update_last=0;
    stopthinking=false;
    
    params->uct_initial_playouts=(int)movetree->getPlayouts();
    params->thread_job=Parameters::TJ_PONDER;
    threadpool->startAll();
    threadpool->waitAll();
    if (movetree->isTerminalResult())
      gtpe->getOutput()->printfDebug("SOLVED! found 100%% sure result after %d plts!\n",(int)movetree->getPlayouts()-params->uct_initial_playouts);
    //fprintf(stderr,"pondering done! %d %.0f\n",playouts,movetree->getPlayouts());
  }
}

void Engine::ponderThread(Worker::Settings *settings)
{
  if (!(params->pondering_enabled) || (currentboard->getMovesMade()<=0) || (currentboard->getPassesPlayed()>=2) || (currentboard->getLastMove().isResign()) || (book->getMoves(boardsize,movehistory).size()>0))
    return;
  
  if (params->move_policy==Parameters::MP_UCT || params->move_policy==Parameters::MP_ONEPLY)
  {
    //fprintf(stderr,"pondering starting!\n");
    this->allowContinuedPlay();
    params->uct_slow_update_last=0;
    
    Go::BitBoard *firstlist=new Go::BitBoard(boardsize);
    Go::BitBoard *secondlist=new Go::BitBoard(boardsize);
    Go::BitBoard *earlyfirstlist=new Go::BitBoard(boardsize);
    Go::BitBoard *earlysecondlist=new Go::BitBoard(boardsize);
    long playouts;
    
    while (!stoppondering && !stopthinking && (playouts=(long)movetree->getPlayouts())<(params->pondering_playouts_max))
    {
      if (movetree->isTerminalResult())
      {
        stopthinking=true;
        break;
      }
      
      params->uct_slow_debug_last=0; // don't print out debug info when pondering
      this->doPlayout(settings,firstlist,secondlist,earlyfirstlist,earlysecondlist);
      playouts++;
    }
    
    delete firstlist;
    delete secondlist;
    delete earlyfirstlist;
    delete earlysecondlist;
    //fprintf(stderr,"pondering done! %d %.0f\n",playouts,movetree->getPlayouts());
  }
}

void Engine::doSlowUpdate()
{
  Tree *besttree=movetree->getRobustChild();
  if (besttree!=NULL)
  {
    params->surewin_expected=(besttree->getRatio()>=params->surewin_threshold);
    
    if (params->surewin_expected && (params->surewin_pass_bonus>0 || params->surewin_touchdead_bonus>0 || params->surewin_oppoarea_penalty>0))
    {
      Tree *passtree=movetree->getChild(Go::Move(currentboard->nextToMove(),Go::Move::PASS));
      
      if (passtree->isPruned())
      {
        passtree->setPruned(false);
        if (params->surewin_pass_bonus>0)
          passtree->setProgressiveBiasBonus(params->surewin_pass_bonus);
        
        if (params->surewin_touchdead_bonus>0)
        {
          int size=boardsize;
          for(std::list<Tree*>::iterator iter=movetree->getChildren()->begin();iter!=movetree->getChildren()->end();++iter) 
          {
            int pos=(*iter)->getMove().getPosition();
            Go::Color col=(*iter)->getMove().getColor();
            Go::Color othercol=Go::otherColor(col);
            
            bool founddead=false;
            foreach_adjacent(pos,p,{
              if (currentboard->getColor(p)==othercol && !currentboard->isAlive(territorymap,params->territory_threshold,p))
                founddead=true;
            });
            
            if (founddead)
            {
              (*iter)->setPruned(false);
              (*iter)->setProgressiveBiasBonus(params->surewin_touchdead_bonus);
            }
          }
        }
        
        if (params->surewin_oppoarea_penalty>0)
        {
          for(std::list<Tree*>::iterator iter=movetree->getChildren()->begin();iter!=movetree->getChildren()->end();++iter) 
          {
            int pos=(*iter)->getMove().getPosition();
            Go::Color col=(*iter)->getMove().getColor();
            
            bool oppoarea=false;
            if (col==Go::BLACK)
              oppoarea=(-territorymap->getPositionOwner(pos))>params->territory_threshold;
            else
              oppoarea=territorymap->getPositionOwner(pos)>params->territory_threshold;
            
            if (oppoarea)
              (*iter)->setProgressiveBiasBonus(-params->surewin_oppoarea_penalty);
          }
        }
      }
    }
    
    params->uct_last_r2=besttree->secondBestPlayoutRatio();
  }
}

void Engine::generateThread(Worker::Settings *settings)
{
  boost::posix_time::ptime time_start=this->timeNow();
  Go::Color col=currentboard->nextToMove();
  int livegfxupdate=0;
  float time_allocated;
  long totalplayouts;
  #ifdef HAVE_MPI
    bool mpi_inform_others=true;
    bool mpi_rank_other=(mpirank!=0);
    //int mpi_update_num=0;
  #else
    bool mpi_rank_other=false;
  #endif
  
  if (!time->isNoTiming())
  {
    if (params->time_ignore)
      time_allocated=0;
    else
      time_allocated=time->getAllocatedTimeForNextTurn(col);
  }
  else
    time_allocated=0;
  
  Go::BitBoard *firstlist=new Go::BitBoard(boardsize);
  Go::BitBoard *secondlist=new Go::BitBoard(boardsize);
  Go::BitBoard *earlyfirstlist=new Go::BitBoard(boardsize);
  Go::BitBoard *earlysecondlist=new Go::BitBoard(boardsize);
  
  while ((totalplayouts=(long)(movetree->getPlayouts()-params->uct_initial_playouts))<(params->playouts_per_move_max))
  {
    if (totalplayouts>=(params->playouts_per_move) && time_allocated==0)
      break;
    else if (totalplayouts>=(params->playouts_per_move_min) && time_allocated>0 && this->timeSince(time_start)>time_allocated)
      break;
    else if (movetree->isTerminalResult())
    {
      params->early_stop_occured=true;
      break;
    }
    else if (stopthinking)
    {
      params->early_stop_occured=true;
      break;
    }
    else if (this->timeSince(time_start)>params->time_move_max)
    {
      params->early_stop_occured=true;
      break;
    }
    
    this->doPlayout(settings,firstlist,secondlist,earlyfirstlist,earlysecondlist);
    totalplayouts+=1;
    
    #ifdef HAVE_MPI
      if (settings->thread->getID()==0 && mpiworldsize>1 && MPI::Wtime()>(params->mpi_last_update+params->mpi_update_period))
      {
        //mpi_update_num++;
        //gtpe->getOutput()->printfDebug("update (%d) at %lf (rank: %d) start\n",mpi_update_num,MPI::Wtime(),mpirank);
        
        mpi_inform_others=this->mpiSyncUpdate();
        
        params->mpi_last_update=MPI::Wtime();
        
        if (!mpi_inform_others)
        {
          params->early_stop_occured=true;
          break;
        }
      }
    #endif
    
    if (settings->thread->getID()==0 && !mpi_rank_other && params->uct_stop_early && params->uct_slow_update_last==0 && totalplayouts>=(params->playouts_per_move_min))
    {
      Tree *besttree=movetree->getRobustChild();
      if (besttree!=NULL)
      {
        double currentpart=(besttree->getPlayouts()-besttree->secondBestPlayouts())/totalplayouts;
        double overallratio,overallratiotimed;
        if (time_allocated>0) // timed search
        {
          overallratio=(double)params->playouts_per_move_max/totalplayouts;
          overallratiotimed=(double)(time_allocated+TIME_RESOLUTION)/this->timeSince(time_start);
        }
        else
        {
          overallratio=(double)params->playouts_per_move/totalplayouts;
          overallratiotimed=0;
        }
        
        if (((overallratio-1)<currentpart) || ((time_allocated>0) && ((overallratiotimed-1)<currentpart)))
        {
          gtpe->getOutput()->printfDebug("best move cannot change! (%.3f %.3f)\n",currentpart,overallratio);
          stopthinking=true;
          params->early_stop_occured=true;
          break;
        }
      }
    }
    
    if (settings->thread->getID()==0 && params->livegfx_on)
    {
      if (livegfxupdate>=(params->livegfx_update_playouts-1))
      {
        livegfxupdate=0;
        
        this->displayPlayoutLiveGfx(totalplayouts);
        
        boost::timer delay;
        while (delay.elapsed()<params->livegfx_delay) {}
      }
      else
        livegfxupdate++;
    }
  }
  
  delete firstlist;
  delete secondlist;
  delete earlyfirstlist;
  delete earlysecondlist;
    
  #ifdef HAVE_MPI
  //gtpe->getOutput()->printfDebug("genmove on rank %d stopping... (inform: %d)\n",mpirank,mpi_inform_others);
  if (settings->thread->getID()==0 && mpiworldsize>1 && mpi_inform_others)
    this->mpiSyncUpdate(true);
  #endif
  
  //stopthinking=true;
}

void Engine::doNPlayoutsThread(Worker::Settings *settings)
{
  int livegfxupdate=0;
  Go::BitBoard *firstlist=new Go::BitBoard(boardsize);
  Go::BitBoard *secondlist=new Go::BitBoard(boardsize);
  Go::BitBoard *earlyfirstlist=new Go::BitBoard(boardsize);
  Go::BitBoard *earlysecondlist=new Go::BitBoard(boardsize);
  long totalplayouts;
  
  while ((totalplayouts=(long)(movetree->getPlayouts()-params->uct_initial_playouts))<(params->playouts_per_move))
  {
    if (movetree->isTerminalResult())
    {
      stopthinking=true;
      break;
    }
    else if (stopthinking)
      break;
    
    this->doPlayout(settings,firstlist,secondlist,earlyfirstlist,earlysecondlist);
    totalplayouts+=1;
    
    if (settings->thread->getID()==0 && params->livegfx_on)
    {
      if (livegfxupdate>=(params->livegfx_update_playouts-1))
      {
        livegfxupdate=0;
        
        this->displayPlayoutLiveGfx(totalplayouts+1);
        
        boost::timer delay;
        while (delay.elapsed()<params->livegfx_delay) {}
      }
      else
        livegfxupdate++;
    }
  }
  
  delete firstlist;
  delete secondlist;
  delete earlyfirstlist;
  delete earlysecondlist;  
}

void Engine::doThreadWork(Worker::Settings *settings)
{
  switch (params->thread_job)
  {
    case Parameters::TJ_GENMOVE:
      this->generateThread(settings);
      break;
    case Parameters::TJ_PONDER:
      this->ponderThread(settings);
      break;
    case Parameters::TJ_DONPLTS:
      this->doNPlayoutsThread(settings);
      break;
  }
}

void Engine::updateTerritoryScoringInTree()
{
  if (!params->rules_all_stones_alive)
  {
    float scorenow;
    if (params->cleanup_in_progress)
      scorenow=currentboard->score()-params->engine->getHandiKomi();
    else
      scorenow=currentboard->territoryScore(territorymap,params->territory_threshold)-params->engine->getHandiKomi();
    
    Go::Color col=currentboard->nextToMove();
    Go::Color othercol=Go::otherColor(col);
    
    bool winforcol=Go::Board::isWinForColor(col,scorenow);
    bool jigonow=(scorenow==0);
    
    Tree *passtree=movetree->getChild(Go::Move(col,Go::Move::PASS));
    if (passtree!=NULL)
    {
      passtree->resetNode();
      if (jigonow)
        passtree->addPartialResult(0.5,1,false);
      else if (winforcol)
        passtree->addWin(scorenow);
      else
        passtree->addLose(scorenow);
      
      Tree *pass2tree=passtree->getChild(Go::Move(othercol,Go::Move::PASS));
      if (pass2tree!=NULL)
      {
        pass2tree->resetNode();
        if (jigonow)
          pass2tree->addPartialResult(0.5,1,false);
        else if (!winforcol)
          pass2tree->addWin(scorenow);
        else
          pass2tree->addLose(scorenow);
      }
    }
  }
}

std::string Engine::chat(bool pm,std::string name,std::string msg)
{
  if (msg=="stat")
  {
    if (moveexplanations->size()>0)
      return moveexplanations->back();
    else
      return "";
  }
  else
    return ("Unknown command '"+msg+"'. Try 'stat'.");
}

void Engine::gameFinished()
{
  if (isgamefinished)
    return;
  isgamefinished=true;

  if (params->mm_learn_enabled) 
  {
    fprintf(stderr,"files gamma %s circ %s\n",learn_filename_features.c_str(),learn_filename_circ_patterns.c_str()); 
    getFeatures()->saveGammaFile (learn_filename_features);
    getFeatures()->saveCircValueFile (learn_filename_circ_patterns);
    gtpe->getOutput()->printfDebug("learned gammas and circ patterns saved with orderquality\n");
  }
  
  if (currentboard->getMovesMade()==0)
    return;

  if (params->auto_save_sgf)
  {
    boost::posix_time::time_facet *facet = new boost::posix_time::time_facet("_%Y-%m-%d_%H:%M:%S");
    std::ostringstream ss;
    ss << params->auto_save_sgf_prefix;
    ss << "game";
    ss.imbue(std::locale(ss.getloc(),facet));
    ss << boost::posix_time::second_clock::local_time();
    ss << ".sgf";
    std::string filename=ss.str();
    if (this->writeGameSGF(filename))
      gtpe->getOutput()->printfDebug("Wrote game SGF to '%s'\n",filename.c_str());
  }
}

#ifdef HAVE_MPI
void Engine::mpiCommandHandler()
{
  Engine::MPICommand cmd;
  unsigned int tmp1,tmp2,tmp3;
  bool running=true;
  
  //gtpe->getOutput()->printfDebug("mpi rank %d reporting for duty!\n",mpirank);
  
  while (running)
  {
    MPI::COMM_WORLD.Bcast(&tmp1,1,MPI::UNSIGNED,0);
    cmd=(Engine::MPICommand)tmp1;
    //gtpe->getOutput()->printfDebug("recv cmd: %d (%d)\n",cmd,mpirank);
    switch (cmd)
    {
      case MPICMD_CLEARBOARD:
        this->clearBoard();
        break;
      case MPICMD_SETBOARDSIZE:
        this->mpiRecvBroadcastedArgs(&tmp1);
        this->setBoardSize((int)tmp1);
        break;
      case MPICMD_SETKOMI:
        this->mpiRecvBroadcastedArgs(&tmp1);
        this->setKomi((float)tmp1);
        break;
      case MPICMD_MAKEMOVE:
        {
          this->mpiRecvBroadcastedArgs(&tmp1,&tmp2);
          Go::Move move = Go::Move((Go::Color)tmp1,(int)tmp2);
          this->makeMove(move);
        }
        break;
      case MPICMD_SETPARAM:
        {
          std::string param=this->mpiRecvBroadcastedString();
          std::string val=this->mpiRecvBroadcastedString();
          params->setParameter(param,val);
          //gtpe->getOutput()->printfDebug("rank of %d set %s to %s\n",mpirank,param.c_str(),val.c_str());
        }
        break;
      case MPICMD_TIMESETTINGS:
        this->mpiRecvBroadcastedArgs(&tmp1,&tmp2,&tmp3);
        delete time;
        time=new Time(params,(int)tmp1,(int)tmp2,(int)tmp3);
        break;
      case MPICMD_TIMELEFT:
        this->mpiRecvBroadcastedArgs(&tmp1,&tmp2,&tmp3);
        time->updateTimeLeft((Go::Color)tmp1,(float)tmp2,(int)tmp3);
        break;
      case MPICMD_LOADPATTERNS:
        delete patterntable;
        patterntable=new Pattern::ThreeByThreeTable();
        patterntable->loadPatternFile(this->mpiRecvBroadcastedString());
        break;
      case MPICMD_CLEARPATTERNS:
        delete patterntable;
        patterntable=new Pattern::ThreeByThreeTable();
        break;
      case MPICMD_LOADFEATUREGAMMAS:
        delete features;
        features=new Features(params);
        features->loadGammaFile(this->mpiRecvBroadcastedString());
        break;
      case MPICMD_BOOKADD:
        {
          this->mpiRecvBroadcastedArgs(&tmp1);
          Go::Move move = Go::Move(currentboard->nextToMove(),(int)tmp1);
          book->add(boardsize,movehistory,move);
        }
        break;
      case MPICMD_BOOKREMOVE:
        {
          this->mpiRecvBroadcastedArgs(&tmp1);
          Go::Move move = Go::Move(currentboard->nextToMove(),(int)tmp1);
          book->remove(boardsize,movehistory,move);
        }
        break;
      case MPICMD_BOOKCLEAR:
        book->clear(boardsize);
        break;
      case MPICMD_BOOKLOAD:
        delete book;
        book=new Book(params);
        book->loadFile(this->mpiRecvBroadcastedString());
        break;
      case MPICMD_CLEARTREE:
        this->clearMoveTree();
        break;
      case MPICMD_GENMOVE:
        this->mpiRecvBroadcastedArgs(&tmp1);
        this->mpiGenMove((Go::Color)tmp1);
        break;
      case MPICMD_QUIT:
      default:
        running=false;
        break;
    };
  }
}


void Engine::mpiBroadcastCommand(Engine::MPICommand cmd, unsigned int *arg1, unsigned int *arg2, unsigned int *arg3)
{
  //gtpe->getOutput()->printfDebug("send cmd: %d (%d)\n",cmd,mpirank);
  MPI::COMM_WORLD.Bcast(&cmd,1,MPI::UNSIGNED,0);
  
  if (arg1!=NULL)
    MPI::COMM_WORLD.Bcast(arg1,1,MPI::UNSIGNED,0);
  if (arg2!=NULL)
    MPI::COMM_WORLD.Bcast(arg2,1,MPI::UNSIGNED,0);
  if (arg3!=NULL)
    MPI::COMM_WORLD.Bcast(arg3,1,MPI::UNSIGNED,0);
}

void Engine::mpiRecvBroadcastedArgs(unsigned int *arg1, unsigned int *arg2, unsigned int *arg3)
{
  if (arg1!=NULL)
    MPI::COMM_WORLD.Bcast(arg1,1,MPI::UNSIGNED,0);
  if (arg2!=NULL)
    MPI::COMM_WORLD.Bcast(arg2,1,MPI::UNSIGNED,0);
  if (arg3!=NULL)
    MPI::COMM_WORLD.Bcast(arg3,1,MPI::UNSIGNED,0);
}

void Engine::mpiBroadcastString(std::string input)
{
  char buffer[MPI_STRING_MAX];
  
  if (input.length()>=MPI_STRING_MAX)
  {
    gtpe->getOutput()->printfDebug("string too long (%d>=%d)\n",input.length(),MPI_STRING_MAX);
    return;
  }
  
  for (int i=0;i<(int)input.length();i++)
  {
    buffer[i]=input.at(i);
  }
  for (int i=input.length();i<MPI_STRING_MAX;i++)
  {
    buffer[i]=0;
  }
  
  MPI::COMM_WORLD.Bcast(buffer,MPI_STRING_MAX,MPI::CHAR,0);
}

std::string Engine::mpiRecvBroadcastedString()
{
  char buffer[MPI_STRING_MAX];
  MPI::COMM_WORLD.Bcast(buffer,MPI_STRING_MAX,MPI::CHAR,0);
  return std::string(buffer);
}

void Engine::mpiSendString(int destrank, std::string input)
{
  char buffer[MPI_STRING_MAX];
  
  if (input.length()>=MPI_STRING_MAX)
  {
    gtpe->getOutput()->printfDebug("string too long (%d>=%d)\n",input.length(),MPI_STRING_MAX);
    return;
  }
  
  for (int i=0;i<(int)input.length();i++)
  {
    buffer[i]=input.at(i);
  }
  for (int i=input.length();i<MPI_STRING_MAX;i++)
  {
    buffer[i]=0;
  }
  
  MPI::COMM_WORLD.Send(buffer,MPI_STRING_MAX,MPI::CHAR,destrank,0);
}

std::string Engine::mpiRecvString(int srcrank)
{
  char buffer[MPI_STRING_MAX];
  MPI::COMM_WORLD.Recv(buffer,MPI_STRING_MAX,MPI::CHAR,srcrank,0);
  return std::string(buffer);
}

void Engine::mpiGenMove(Go::Color col)
{
  //gtpe->getOutput()->printfDebug("genmove on rank %d starting...\n",mpirank);
  currentboard->setNextToMove(col);
  
  movetree->pruneSuperkoViolations();
  this->allowContinuedPlay();
  this->updateTerritoryScoringInTree();
  params->uct_slow_update_last=0;
  params->uct_slow_debug_last=0;
  params->uct_last_r2=-1;
  
  int startplayouts=(int)movetree->getPlayouts();
  params->mpi_last_update=MPI::Wtime();
  
  params->uct_initial_playouts=startplayouts;
  params->thread_job=Parameters::TJ_GENMOVE;
  threadpool->startAll();
  threadpool->waitAll();
  
  //gtpe->getOutput()->printfDebug("genmove on rank %d done.\n",mpirank);
}

void Engine::mpiBuildDerivedTypes()
{
  //mpistruct_updatemsg tmp;
  int i=0,count=4;
  int blocklengths[count];
  MPI::Datatype types[count];
  MPI::Aint displacements[count];
  MPI::Aint extent,lowerbound;
  
  blocklengths[i]=8;
  types[i]=MPI::BYTE;
  displacements[i]=0;
  types[i].Get_extent(lowerbound,extent);
  i++;
  //if (mpirank==0)
  //  fprintf(stderr,"lowerbound: %d, extent: %d\n",lowerbound,extent);
  
  blocklengths[i]=8;
  types[i]=MPI::BYTE;
  displacements[i]=displacements[i-1]+extent*blocklengths[i-1];
  types[i].Get_extent(lowerbound,extent);
  i++;
  //if (mpirank==0)
  //  fprintf(stderr,"lowerbound: %d, extent: %d\n",lowerbound,extent);
  
  blocklengths[i]=1;
  types[i]=MPI::FLOAT;
  displacements[i]=displacements[i-1]+extent*blocklengths[i-1];
  types[i].Get_extent(lowerbound,extent);
  i++;
  //if (mpirank==0)
  //  fprintf(stderr,"lowerbound: %d, extent: %d\n",lowerbound,extent);
  
  blocklengths[i]=1;
  types[i]=MPI::FLOAT;
  displacements[i]=displacements[i-1]+extent*blocklengths[i-1];
  types[i].Get_extent(lowerbound,extent);
  i++;
  //if (mpirank==0)
  //  fprintf(stderr,"lowerbound: %d, extent: %d\n",lowerbound,extent);
  
  //TODO: verify that above works if struct elements aren't contiguous (word boundaries)
  
  mpitype_updatemsg=MPI::Datatype::Create_struct(count,blocklengths,displacements,types);
  mpitype_updatemsg.Commit();
}

void Engine::mpiFreeDerivedTypes()
{
  mpitype_updatemsg.Free();
}

void Engine::MpiHashTable::clear()
{
  for (int i=0;i<MPI_HASHTABLE_SIZE;i++)
  {
    table[i].clear();
  }
}

Engine::MpiHashTable::TableEntry *Engine::MpiHashTable::lookupEntry(Go::ZobristHash hash)
{
  unsigned int index=hash%MPI_HASHTABLE_SIZE;
  for(std::list<Engine::MpiHashTable::TableEntry>::iterator iter=table[index].begin();iter!=table[index].end();++iter)
  {
    if (hash==(*iter).hash)
      return &(*iter);
  }
  return NULL;
}

std::list<Tree*> *Engine::MpiHashTable::lookup(Go::ZobristHash hash)
{
  Engine::MpiHashTable::TableEntry *entry=this->lookupEntry(hash);
  if (entry!=NULL)
    return &(entry->nodes);
  else
    return NULL;
}

void Engine::MpiHashTable::add(Go::ZobristHash hash, Tree *node)
{
  Engine::MpiHashTable::TableEntry *entry=this->lookupEntry(hash);
  if (entry!=NULL)
  {
    for(std::list<Tree*>::iterator iter=entry->nodes.begin();iter!=entry->nodes.end();++iter)
    {
      if ((*iter)==node) // is already there?
        return;
    }
    entry->nodes.push_back(node);
  }
  else
  {
    unsigned int index=hash%MPI_HASHTABLE_SIZE;
    Engine::MpiHashTable::TableEntry entry;
    entry.hash=hash;
    entry.nodes.push_back(node);
    table[index].push_back(entry);
  }
}

bool Engine::mpiSyncUpdate(bool stop)
{
  int localcount=(stop?0:1);
  int maxcount;
  
  //gtpe->getOutput()->printfDebug("sync (rank: %d) (stop:%d)!!!!!\n",mpirank,stop);
  
  //TODO: should consider replacing first 2 mpi cmds with 1
  MPI::COMM_WORLD.Allreduce(&localcount,&maxcount,1,MPI::INT,MPI::MIN);
  if (maxcount==0)
  {
    //gtpe->getOutput()->printfDebug("sync (rank: %d) stopping\n",mpirank);
    return false;
  }
  //gtpe->getOutput()->printfDebug("sync (rank: %d) not stopping\n",mpirank);
  
  std::list<mpistruct_updatemsg> locallist;
  if (movetree->getPlayouts()>0)
  {
    float threshold=params->mpi_update_threshold*movetree->getPlayouts();
    this->mpiFillList(locallist,threshold,params->mpi_update_depth,movetree);
  }
  
  if (locallist.size()==0) // add 1 empty msg, else MPI_Allgather() will stall
  {
    mpistruct_updatemsg msg;
    msg.hash=0;
    msg.parenthash=0;
    msg.playouts=0;
    msg.wins=0;
    locallist.push_back(msg);
    //gtpe->getOutput()->printfDebug("sync (rank: %d) added empty msg\n",mpirank);
  }
  
  localcount=0;
  mpistruct_updatemsg localmsgs[locallist.size()];
  for(std::list<mpistruct_updatemsg>::iterator iter=locallist.begin();iter!=locallist.end();++iter)
  {
    localmsgs[localcount]=(*iter);
    localcount++;
  }
  
  MPI::COMM_WORLD.Allreduce(&localcount,&maxcount,1,MPI::INT,MPI::MAX);
  //gtpe->getOutput()->printfDebug("sync (rank: %d) (local:%d, max:%d)\n",mpirank,localcount,maxcount);
  
  mpistruct_updatemsg allmsgs[maxcount*mpiworldsize];
  for (int i=0;i<maxcount*mpiworldsize;i++)
  {
    allmsgs[i].hash=0; // needed as maxcount is not necessarily mincount
  }
  MPI::COMM_WORLD.Allgather(localmsgs,localcount,mpitype_updatemsg,allmsgs,maxcount,mpitype_updatemsg);
  //gtpe->getOutput()->printfDebug("sync (rank: %d) gathered\n",mpirank);
  
  for (int i=0;i<mpiworldsize;i++)
  {
    if (i==mpirank) //ignore own messages
      continue;
    
    for (int j=0;j<maxcount;j++)
    {
      mpistruct_updatemsg *msg=&allmsgs[i*maxcount+j];
      if (msg->hash==0)
        break;
      
      //fprintf(stderr,"add msg: 0x%016Lx 0x%016Lx %.1f %.1f\n",msg->hash,msg->parenthash,msg->playouts,msg->wins);
      
      std::list<Tree*> *nodes=mpihashtable.lookup(msg->hash);
      if (nodes!=NULL)
      {
        for(std::list<Tree*>::iterator iter=nodes->begin();iter!=nodes->end();++iter)
        {
          (*iter)->addMpiDiff(msg->playouts,msg->wins);
        }
      }
      else
      {
        bool foundnode=false;
        std::list<Tree*> *parentnodes=mpihashtable.lookup(msg->parenthash);
        if (parentnodes!=NULL)
        {
          for(std::list<Tree*>::iterator iter=parentnodes->begin();iter!=parentnodes->end();++iter)
          {
            for(std::list<Tree*>::iterator iter2=(*iter)->getChildren()->begin();iter2!=(*iter)->getChildren()->end();++iter2)
            {
              if (msg->hash==(*iter2)->getHash())
              {
                (*iter2)->addMpiDiff(msg->playouts,msg->wins);
                mpihashtable.add(msg->hash,(*iter2));
                foundnode=true;
                //fprintf(stderr,"added hash: 0x%016Lx\n",msg->hash);
              }
            }
          }
        }
        
        //if (!foundnode)
        //  fprintf(stderr,"node not found! (0x%016Lx)\n",msg->hash);
      }
    }
  }
  
  //gtpe->getOutput()->printfDebug("sync (rank: %d) done\n",mpirank);
  
  return true;
}

void Engine::mpiFillList(std::list<mpistruct_updatemsg> &list, float threshold, int depthleft, Tree *tree)
{
  if (depthleft<=0)
    return;
  
  //fprintf(stderr,"adding nodes (%d)\n",depthleft);
  
  Go::ZobristHash parenthash=tree->getHash();
  if (tree->isRoot())
    mpihashtable.add(parenthash,tree);
  
  for(std::list<Tree*>::iterator iter=tree->getChildren()->begin();iter!=tree->getChildren()->end();++iter)
  {
    if ((*iter)->getPlayouts()>=threshold && (*iter)->getHash()!=0)
    {
      mpistruct_updatemsg msg;
      msg.hash=(*iter)->getHash();
      msg.parenthash=parenthash;
      (*iter)->fetchMpiDiff(msg.playouts,msg.wins);
      list.push_back(msg);
      mpihashtable.add(msg.hash,(*iter));
      this->mpiFillList(list,threshold,depthleft-1,(*iter));
    }
  }
}

#endif

float Engine::getOldMoveValue(Go::Move m)
{
  if (m.isNormal ())
  {
    if (m.getColor ()==Go::BLACK)
    {
      if (blackOldMoves[m.getPosition ()]-blackOldMean>0)
        return (blackOldMoves[m.getPosition ()]-blackOldMean)*pow(whiteOldMovesNum,params->uct_oldmove_unprune_factor_c);
      else
        return 0; //do not allow negative results ??? If not using this, one must take care of the Pruned childs!!!!!!
    }
    else
    {
      if (whiteOldMoves[m.getPosition ()]-blackOldMean>0)
        return (whiteOldMoves[m.getPosition ()]-whiteOldMean)*pow(blackOldMovesNum,params->uct_oldmove_unprune_factor_c);
      else
        return 0; //do not allow negative results ??? If not using this, one must take care of the Pruned childs!!!!!!
    }
  }
  else
    return 0; //was a pass move
}
    
