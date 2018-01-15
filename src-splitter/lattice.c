/*
 * ��Ψ��ɾ�����ӥ��ӥ��르�ꥺ��(viterbi algorithm)�ˤ�ä�
 * ʸ��ζ��ڤ����ꤷ�ƥޡ������롣
 *
 *
 * ��������ƤӽФ����ؿ�
 *  anthy_mark_borders()
 *
 * Copyright (C) 2006-2007 TABATA Yusuke
 * Copyright (C) 2004-2006 YOSHIDA Yuichi
 * Copyright (C) 2006 HANAOKA Toshiyuki
 * 
 */
/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
/*
 * ����ƥ��������¸�ߤ���meta_word��Ĥʤ��ǥ���դ������ޤ���
 * (���Υ���դΤ��Ȥ��ƥ���(lattice/«)�⤷���ϥȥ�ꥹ(trellis)�ȸƤӤޤ�)
 * meta_word�ɤ�������³������դΥΡ��ɤȤʤꡢ��¤��lattice_node��
 * ��󥯤Ȥ��ƹ�������ޤ���
 *
 * �����Ǥν����ϼ�����Ĥ����Ǥǹ�������ޤ�
 * (1) ����դ������Ĥġ��ƥΡ��ɤؤ���ã��Ψ�����
 * (2) ����դ���(��)���餿�ɤäƺ�Ŭ�ʥѥ������
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <anthy/alloc.h>
#include <anthy/xstr.h>
#include <anthy/segclass.h>
#include <anthy/splitter.h>
#include <anthy/feature_set.h>
#include <anthy/diclib.h>
#include "wordborder.h"

static void *trans_info_array;
static void *seg_info_array;
static void *yomi_info_array;
static void *seg_len_info_array;

#define NODE_MAX_SIZE 50

/* ����դΥΡ���(���ܾ���) */
struct lattice_node {
  int node_id;
  int border; /* ʸ������Τɤ�����Ϥޤ�Ρ��ɤ� */
  enum seg_class seg_class; /* ���ξ��֤��ʻ� */


  double node_probability; /* ���ΥΡ��ɤ�����γ�Ψ */
  double path_probability;  /* �����˻��ޤǤγ�Ψ */

  struct lattice_node* before_node; /* ������ΥΡ��� */
  struct meta_word* mw; /* ���ΥΡ��ɤ��б�����meta_word */

  struct lattice_node* next; /* �ꥹ�ȹ�¤�Τ���Υݥ��� */
};

struct node_list_head {
  struct lattice_node *head;
  int nr_nodes;
};

struct lattice_info {
  /* ���ܾ��֤Υꥹ�Ȥ����� */
  int node_id;
  struct node_list_head *lattice_node_list;
  struct splitter_context *sc;
  /* �Ρ��ɤΥ������� */
  allocator node_allocator;
  int last_node_id;
};

static double get_transition_probability(struct lattice_node *node);
/*
 */
static void
print_lattice_node(struct lattice_info *info, struct lattice_node *node)
{
  struct lattice_node *before_node;
  if (!node) {
    printf("**lattice_node (null)*\n");
    return ;
  }
  before_node = node->before_node;
  printf("**lattice_node id:%d(<-%d) path_p=%f,node_p=%f(<-%f)\n",
	 node->node_id, (before_node ? before_node->node_id : 0),
	 node->path_probability,
	 node->node_probability,
	 (before_node ? before_node->path_probability : 1.0f));
  if (node->mw) {
    anthy_print_metaword(info->sc, node->mw);
  }
  printf("\n");
}

static void
build_feature_list(struct lattice_node *node,
		   struct lattice_node *before_node,
		   struct feature_list *features)
{
  int pc, cc;
  if (node) {
    cc = node->seg_class;
  } else {
    cc = SEG_TAIL;
  }
  anthy_feature_list_set_cur_class(features, cc);
  if (before_node) {
    pc = before_node->seg_class;
  } else {
    pc = SEG_HEAD;
  }
  anthy_feature_list_set_class_trans(features, pc, cc);
  
  if (node && node->mw) {
    struct meta_word *mw = node->mw;
    anthy_feature_list_set_dep_class(features, mw->dep_class);
    anthy_feature_list_set_dep_word(features,
				    mw->dep_word_hash);
    anthy_feature_list_set_mw_features(features, mw->mw_features);
    anthy_feature_list_set_core_wtype(features, mw->core_wt);
    anthy_feature_list_set_yomi_hash(features, mw->yomi_hash);
  }
  anthy_feature_list_sort(features);
}

static double
search_probability(void *array, struct feature_list *fl, double noscore)
{
  double prob;
  struct feature_freq *res, arg;

  /* ��Ψ��׻����� */
  res = anthy_find_feature_freq(array, fl, &arg);
  if (res) {
    double pos = res->f[15];
    double neg = res->f[14];
    prob = 1 - (neg) / (double) (pos + neg);
    if (prob < 0) {
      prob = noscore;
    }
  } else {
    prob = noscore;
  }
  return prob;
}

static void
debug_ln(struct lattice_node *node,
	 double probability, double p)
{
  if (node) {
    printf(" cc=%d(%s), P=%f(%f)\n", node->seg_class,
	   anthy_seg_class_name(node->seg_class), probability, p);
  } else {
    printf(" cc=%d(%s), P=%f(%f)\n", SEG_TAIL,
	   anthy_seg_class_name(SEG_TAIL), probability, p);
  }
}

static double
get_transition_probability(struct lattice_node *node)
{
  struct feature_list seg_features;
  struct feature_list seg_trans_features;
  struct feature_list seg_struct_features;
  struct feature_list seg_len_features;
  double probability, p;
  double trans_p, seg_p, all_p, len_p;

  /* �����������Υꥹ�Ȥ��� */
  anthy_feature_list_init(&seg_features, FL_SEG_FEATURES);
  build_feature_list(node, node->before_node, &seg_features);
  /* ʸ����³�˴ؤ��������ꥹ�� */
  anthy_feature_list_clone(&seg_trans_features, &seg_features, FL_SEG_TRANS_FEATURES);
  anthy_feature_list_clone(&seg_struct_features, &seg_features, FL_SEG_STRUCT_FEATURES);
  /**/
  anthy_feature_list_clone(&seg_len_features, &seg_features, FL_SEG_LEN_FEATURES);

  /* ���줾����Ѥǳ�Ψ��׻����� */
  probability = 1;
  trans_p = search_probability(trans_info_array, &seg_trans_features, 0.4f);
  seg_p = search_probability(seg_info_array, &seg_struct_features, 0.5f);
  all_p = search_probability(yomi_info_array, &seg_features, 1.0f);
  if (trans_p > 0.4f) {
    probability *= trans_p;
  } else {
    probability *= seg_p;
  }
  all_p = (2 + all_p) / 3;
  probability *= all_p;
  
  p = probability;
  len_p = search_probability(seg_len_info_array, &seg_len_features, 0.5f);
  probability *= len_p;


  /**/
  if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
    printf("trans_f=");anthy_feature_list_print(&seg_trans_features);
    printf("seg_f=");anthy_feature_list_print(&seg_struct_features);
    printf("all_f=");anthy_feature_list_print(&seg_features);
    printf("len_f=");anthy_feature_list_print(&seg_len_features);
    debug_ln(node, probability, p);
    printf(" trans_p=%f, seg_p=%f, all_p=%f, len_p=%f\n",
	   trans_p, seg_p, all_p, len_p);
  }

  /**/
  anthy_feature_list_free(&seg_features);
  anthy_feature_list_free(&seg_struct_features);
  anthy_feature_list_free(&seg_len_features);
  anthy_feature_list_free(&seg_trans_features);

  if (1) {
    /* length bias��������ˤ����� */
    probability = (1 - probability) / node->mw->len;
    probability = 1 - probability;
  }

  return probability;
}

static struct lattice_info*
alloc_lattice_info(struct splitter_context *sc, int size)
{
  int i;
  struct lattice_info* info = (struct lattice_info*)malloc(sizeof(struct lattice_info));
  info->sc = sc;
  info->lattice_node_list = (struct node_list_head*)
    malloc((size + 1) * sizeof(struct node_list_head));
  for (i = 0; i < size + 1; i++) {
    info->lattice_node_list[i].head = NULL;
    info->lattice_node_list[i].nr_nodes = 0;
  }
  info->node_allocator = anthy_create_allocator(sizeof(struct lattice_node),
						NULL);
  info->last_node_id = 0;
  return info;
}

static void
calc_node_parameters(struct lattice_node *node)
{
  /* �б�����metaword��̵������ʸƬ��Ƚ�Ǥ��� */
  node->seg_class = node->mw ? node->mw->seg_class : SEG_HEAD; 

  if (node->before_node) {
    /* �������ܤ���Ρ��ɤ������� */
    if (node->mw && (node->mw->mw_features & MW_FEATURE_OCHAIRE)) {
      node->node_probability = 1.0f;
    } else {
      node->node_probability = get_transition_probability(node);
    }
    node->path_probability =
      node->before_node->path_probability *
      node->node_probability;
  } else {
    /* �������ܤ���Ρ��ɤ�̵����� */
    node->path_probability = 1.0f;
  }
}

static struct lattice_node*
alloc_lattice_node(struct lattice_info *info,
		   struct lattice_node* before_node,
		   struct meta_word* mw, int border)
{
  struct lattice_node* node;
  node = anthy_smalloc(info->node_allocator);
  info->last_node_id ++;
  node->node_id = info->last_node_id;
  node->before_node = before_node;
  node->border = border;
  node->next = NULL;
  node->mw = mw;

  calc_node_parameters(node);

  return node;
}

static void 
release_lattice_node(struct lattice_info *info, struct lattice_node* node)
{
  anthy_sfree(info->node_allocator, node);
}

static void
release_lattice_info(struct lattice_info* info)
{
  anthy_free_allocator(info->node_allocator);
  free(info->lattice_node_list);
  free(info);
}

static int
cmp_node_by_type(struct lattice_node *lhs, struct lattice_node *rhs,
		 enum metaword_type type)
{
  if (lhs->mw->type == type && rhs->mw->type != type) {
    return 1;
  } else if (lhs->mw->type != type && rhs->mw->type == type) {
    return -1;
  } else {
    return 0;
  }
}

static int
cmp_node_by_type_to_type(struct lattice_node *lhs, struct lattice_node *rhs,
			 enum metaword_type type1, enum metaword_type type2)
{
  if (lhs->mw->type == type1 && rhs->mw->type == type2) {
    return 1;
  } else if (lhs->mw->type == type2 && rhs->mw->type == type1) {
    return -1;
  } else {
    return 0;
  } 
}

/*
 * �Ρ��ɤ���Ӥ���
 *
 ** �֤���
 * 1: lhs��������Ψ���⤤
 * 0: Ʊ��
 * -1: rhs��������Ψ���⤤
 */
static int
cmp_node(struct lattice_node *lhs, struct lattice_node *rhs)
{
  struct lattice_node *lhs_before = lhs;
  struct lattice_node *rhs_before = rhs;
  int ret;

  if (lhs && !rhs) return 1;
  if (!lhs && rhs) return -1;
  if (!lhs && !rhs) return 0;

  while (lhs_before && rhs_before) {
    if (lhs_before->mw && rhs_before->mw &&
	lhs_before->mw->from + lhs_before->mw->len == rhs_before->mw->from + rhs_before->mw->len) {
      /* �ؽ�������줿�Ρ��ɤ��ɤ����򸫤� */
      ret = cmp_node_by_type(lhs_before, rhs_before, MW_OCHAIRE);
      if (ret != 0) return ret;

      /* COMPOUND_PART����COMPOUND_HEAD��ͥ�� */
      ret = cmp_node_by_type_to_type(lhs_before, rhs_before,
				     MW_COMPOUND_HEAD, MW_COMPOUND_PART);
      if (ret != 0) return ret;
    } else {
      break;
    }
    lhs_before = lhs_before->before_node;
    rhs_before = rhs_before->before_node;
  }

  /* �Ǹ�����ܳ�Ψ�򸫤� */
  if (lhs->path_probability > rhs->path_probability) {
    return 1;
  } else if (lhs->path_probability < rhs->path_probability) {
    return -1;
  } else {
    return 0;
  }
}

/*
 * ������Υ�ƥ����˥Ρ��ɤ��ɲä���
 */
static void
push_node(struct lattice_info* info, struct lattice_node* new_node,
	  int position)
{
  struct lattice_node* node;
  struct lattice_node* previous_node = NULL;

  if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
    print_lattice_node(info, new_node);
  }

  /* ��Ƭ��node��̵�����̵�����ɲ� */
  node = info->lattice_node_list[position].head;
  if (!node) {
    info->lattice_node_list[position].head = new_node;
    info->lattice_node_list[position].nr_nodes ++;
    return;
  }

  while (node->next) {
    /* ;�פʥΡ��ɤ��ɲä��ʤ�����λ޴��� */
    if (new_node->seg_class == node->seg_class &&
	new_node->border == node->border) {
      /* segclass��Ʊ���ǡ��Ϥޤ���֤�Ʊ���ʤ� */
      switch (cmp_node(new_node, node)) {
      case 0:
      case 1:
	/* ������������Ψ���礭�����ؽ��ˤ���Τʤ顢�Ť��Τ��֤�����*/
	if (previous_node) {
	  previous_node->next = new_node;
	} else {
	  info->lattice_node_list[position].head = new_node;
	}
	new_node->next = node->next;
	release_lattice_node(info, node);
	break;
      case -1:
	/* �����Ǥʤ��ʤ��� */
	release_lattice_node(info, new_node);
	break;
      }
      return;
    }
    previous_node = node;
    node = node->next;
  }

  /* �Ǹ�ΥΡ��ɤθ����ɲ� */
  node->next = new_node;
  info->lattice_node_list[position].nr_nodes ++;
}

/* ���ֳ�Ψ���㤤�Ρ��ɤ�õ��*/
static void
remove_min_node(struct lattice_info *info, struct node_list_head *node_list)
{
  struct lattice_node* node = node_list->head;
  struct lattice_node* previous_node = NULL;
  struct lattice_node* min_node = node;
  struct lattice_node* previous_min_node = NULL;

  /* ���ֳ�Ψ���㤤�Ρ��ɤ�õ�� */
  while (node) {
    if (cmp_node(node, min_node) < 0) {
      previous_min_node = previous_node;
      min_node = node;
    }
    previous_node = node;
    node = node->next;
  }

  /* ���ֳ�Ψ���㤤�Ρ��ɤ������� */
  if (previous_min_node) {
    previous_min_node->next = min_node->next;
  } else {
    node_list->head = min_node->next;
  }
  release_lattice_node(info, min_node);
  node_list->nr_nodes --;
}

/* ������ӥ��ӥ��르�ꥺ�����Ѥ��Ʒ�ϩ������ */
static void
choose_path(struct lattice_info* info, int to)
{
  /* �Ǹ�ޤ���ã�������ܤΤʤ��ǰ��ֳ�Ψ���礭����Τ����� */
  struct lattice_node* node;
  struct lattice_node* best_node = NULL;
  int last = to; 
  while (!info->lattice_node_list[last].head) {
    /* �Ǹ��ʸ���ޤ����ܤ��Ƥ��ʤ��ä������� */
    --last;
  }
  for (node = info->lattice_node_list[last].head; node; node = node->next) {
    if (cmp_node(node, best_node) > 0) {
      best_node = node;
    }
  }
  if (!best_node) {
    return;
  }

  /* ���ܤ�դˤ��ɤ�Ĥ�ʸ����ڤ��ܤ�Ͽ */
  node = best_node;
  if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LP) {
    printf("choose_path()\n");
  }
  while (node->before_node) {
    info->sc->word_split_info->best_seg_class[node->border] =
      node->seg_class;
    anthy_mark_border_by_metaword(info->sc, node->mw);
    /**/
    if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LP) {
      get_transition_probability(node);
      print_lattice_node(info, node);
    }
    /**/
    node = node->before_node;
  }
}

static void
build_graph(struct lattice_info* info, int from, int to)
{
  int i;
  struct lattice_node* node;
  struct lattice_node* left_node;

  /* �����Ȥʤ�Ρ��ɤ��ɲ� */
  node = alloc_lattice_node(info, NULL, NULL, from);
  push_node(info, node, from);

  /* info->lattice_node_list[index]�ˤ�index�ޤǤ����ܤ����äƤ���ΤǤ��äơ�
   * index��������ܤ����äƤ���ΤǤϤʤ� 
   */

  /* ���Ƥ����ܤ򺸤��� */
  for (i = from; i < to; ++i) {
    for (left_node = info->lattice_node_list[i].head; left_node;
	 left_node = left_node->next) {
      struct meta_word *mw;
      /* iʸ���ܤ���ã����lattice_node�Υ롼�� */

      for (mw = info->sc->word_split_info->cnode[i].mw; mw; mw = mw->next) {
	int position;
	struct lattice_node* new_node;
	/* iʸ���ܤ����meta_word�Υ롼�� */

	if (mw->can_use != ok) {
	  continue; /* ����줿ʸ��ζ��ڤ��ޤ���metaword�ϻȤ�ʤ� */
	}
	position = i + mw->len;
	new_node = alloc_lattice_node(info, left_node, mw, i);
	push_node(info, new_node, position);

	/* ��θ��䤬¿�������顢��Ψ���㤤�������� */
	if (info->lattice_node_list[position].nr_nodes >= NODE_MAX_SIZE) {
	  remove_min_node(info, &info->lattice_node_list[position]);
	}
      }
    }
  }

  /* ʸ������ */
  for (node = info->lattice_node_list[to].head; node; node = node->next) {
    struct feature_list features;
    double prob;
    anthy_feature_list_init(&features, FL_SEG_TRANS_FEATURES);
    build_feature_list(NULL, node, &features);
    prob = search_probability(trans_info_array, &features, 0.5);
    node->path_probability = node->path_probability *
      prob;
    if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
      printf("trans_f=");anthy_feature_list_print(&features);
      debug_ln(NULL, prob, 0);
    }
    anthy_feature_list_free(&features);
  }
}

void
anthy_mark_borders(struct splitter_context *sc, int from, int to)
{
  struct lattice_info* info = alloc_lattice_info(sc, to);
  trans_info_array = anthy_file_dic_get_section("trans_info");
  seg_info_array = anthy_file_dic_get_section("seg_info");
  yomi_info_array = anthy_file_dic_get_section("yomi_info");
  seg_len_info_array = anthy_file_dic_get_section("seg_len_info");
  build_graph(info, from, to);
  choose_path(info, to);
  release_lattice_info(info);
}
