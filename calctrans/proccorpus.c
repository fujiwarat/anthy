/*
 * �����ѥ��Ȥʤ�ʸ�Ϥ��ɤ�ǡ�ʸ���Ĺ����Ĵ������
 * �����ǲ��Ϥη�̤���Ϥ���
 *
 * ���Ϸ����ˤĤ���
 *  �ޤ����̤�Ԥä�ʸ�᤬�ǽ��Ĺ���ǽ��Ϥ����
 *  ���˳�ʸ�����(�����)��ä����䡢����������ν�Ǿ������Ϥ���
 *
 *
 * Copyright (C) 2006-2007 TABATA Yusuke
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <anthy/convdb.h>

static int verbose;

/* ʸ���Ĺ������ʸ�ˤ��碌�� */
static int
trim_segment(anthy_context_t ac, struct conv_res *cr,
	     int nth, char *seg, void *cookie)
{
  int len = strlen(seg);
  int resized = 0;
  (void)cookie;

  while (1) {
    char seg_buf[1024];
    int cur_len;

    anthy_get_segment(ac, nth, NTH_UNCONVERTED_CANDIDATE, seg_buf, 1024);
    cur_len = strlen(seg_buf);
    if (len == cur_len) {
      return 1;
    }
    if (!resized) {
      struct anthy_conv_stat acs;
      resized = 1;
      /* ��������ʸ��ξ����ɽ������ */
      print_size_miss_segment_info(ac, nth);
      /**/
      anthy_get_stat(ac, &acs);
      if (nth < acs.nr_segment - 1) {
	print_size_miss_segment_info(ac, nth + 1);
      }
    }
    if (len > cur_len) {
      anthy_resize_segment(ac, nth, 1);
    } else {
      anthy_resize_segment(ac, nth, -1);
    }
    cr->check = CHK_MISS;
  }
  return 0;
}

/*
 * nth���ܤ�ʸ��Ǹ���seg��õ���Ƴ��ꤹ��
 */
static int
find_candidate(anthy_context_t ac, struct conv_res *cr,
	       int nth, char *seg, void *cookie)
{
  char seg_buf[1024];
  int i;
  struct anthy_segment_stat ass;
  (void)cookie;

  if (seg[0] == '~') {
    /* ����ߥ��Υޡ�����~�פ򥹥��åפ��� */
    seg++;
    cr->cand_check[nth] = 1;
  }

  anthy_get_segment_stat(ac, nth, &ass);
  for (i = 0; i < ass.nr_candidate; i++) {
    anthy_get_segment(ac, nth, i, seg_buf, 1024);
    if (!strcmp(seg_buf, seg)) {
      /* ���פ������򸫤Ĥ����Τǳ��ꤹ�� */
      anthy_commit_segment(ac, nth, i);
      return 0;
    }
  }
  return 0;
}

/* '|' ��ʸ��˶��ڤ�줿ʸ����γ�ʸ��������fn��Ƥ� */
static int
for_each_segment(anthy_context_t ac, struct conv_res *cr,
		 const char *res_str,
		 int (*fn)(anthy_context_t ac, struct conv_res *cr,
			   int nth, char *seg, void *cookie),
		 void *cookie)
{
  char *str, *cur, *cur_seg;
  int nth;
  int rv;
  if (!res_str) {
    return -1;
  }

  str = strdup(res_str);
  cur = str;
  cur ++;
  cur_seg = cur;
  nth = 0;
  rv = 0;
  while ((cur = strchr(cur, '|'))) {
    *cur = 0;
    /**/
    if (fn) {
      rv += fn(ac, cr, nth, cur_seg, cookie);
    }
    /**/
    nth ++;
    cur ++;
    cur_seg = cur;
  }

  free(str);
  
  return rv;
}

static void
get_current_sentence(anthy_context_t ac, char *buf)
{
  struct anthy_conv_stat acs;
  char seg_buf[1024];
  int i;
  buf[0] = 0;
  if (anthy_get_stat(ac, &acs)) {
    return ;
  }
  for (i = 0; i < acs.nr_segment; i++) {
    anthy_get_segment(ac, i, 0, seg_buf, 1024);
    strcat(buf, seg_buf);
  }
}

static int
compare_segment(anthy_context_t ac, struct conv_res *cr,
		int nth, char *seg, void *cookie)
{
  char **buf = (char **) cookie;
  int seg_len = strlen(seg);
  if (!*buf) {
    return 1;
  }
  if (!strncmp(*buf, seg, seg_len)) {
    *buf += seg_len;
    return 0;
  }
  *buf = NULL;
  return 1;
}

static int
compare_sentence(anthy_context_t ac, struct conv_res *cr)
{
  char conv_buf[1024];
  char *buf_ptr;
  int rv;
  if (!cr->cand_str) {
    return 1;
  }
  get_current_sentence(ac, conv_buf);
  buf_ptr = conv_buf;
  rv = for_each_segment(ac, cr, cr->cand_str,
			compare_segment, (void *)&buf_ptr);
  if (!rv) {
    return 0;
  }
  return 1;
}

static int
fixup_conversion(anthy_context_t ac, struct conv_res *cr)
{
  int i;
  struct anthy_conv_stat acs;
  /* ʸ���Ĺ����Ĵ�᤹�� */
  if (for_each_segment(ac, cr, cr->res_str, trim_segment, NULL) < 0) {
    return -1;
  }
  /**/
  if (anthy_get_stat(ac, &acs)) {
    return -1;
  }
  cr->cand_check = malloc(sizeof(int) * acs.nr_segment);
  for (i = 0; i < acs.nr_segment; i++) {
    cr->cand_check[i] = 0;
  }

  /* ��������򤹤� */
  if (cr->cand_str) {
    for_each_segment(ac, cr, cr->cand_str, find_candidate, NULL);
  }
  return 0;
}

static void
proc_sentence(anthy_context_t ac, struct conv_res *cr, FILE *ofp)
{
  /*printf("(%s)\n", cr->src_str);*/
  anthy_set_string(ac, cr->src_str);
  if (compare_sentence(ac, cr)) {
    /* ʸ��ο��̡�������ѹ���Ԥ� */
    if (fixup_conversion(ac, cr)) {
      return ;
    }
  }

  if (verbose) {
    anthy_print_context(ac);
  }
  if (ofp && cr->check == CHK_MISS) {
    fprintf(ofp, "%s %s\n", cr->res_str, cr->cand_str);
  }
  /* ���Ϥ��� */
  print_context_info(ac, cr);
}

int
main(int argc, char **argv)
{
  struct res_db *db;
  struct conv_res *cr;
  anthy_context_t ac;
  FILE *err_fp = NULL;
  int i, nr;

  db = create_db();
  for (i = 1; i < argc; i++) {
    if (!strcmp("-v", argv[i])) {
      verbose = 1;
    } else if (!strcmp("-e", argv[i]) && i < argc - 1) {
      err_fp = fopen(argv[i+1], "w");
      i++;
    } else {
      read_db(db, argv[i]);
    }
  }

  anthy_conf_override("CONFFILE", "../anthy-conf");
  anthy_conf_override("DIC_FILE", "../mkanthydic/anthy.dic");
  anthy_init();
  anthy_set_personality("");
  ac = anthy_create_context();

  /**/
  nr = 0;
  /**/
  for (cr = db->res_list.next; cr; cr = cr->next) {
    /*fprintf(stderr, "%d:%s\n", nr, cr->res_str);*/
    proc_sentence(ac, cr, err_fp);
    if (!(nr % 100)) {
      fprintf(stderr, "%d\n", nr);
    }
    nr ++;
  }
  return 0;
}
