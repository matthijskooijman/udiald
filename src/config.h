#ifndef UMTS_CONFIG_H_
#define UMTS_CONFIG_H_

#include <libubox/list.h>
#include <libubox/ucix.h>

static inline char* umts_config_get(struct umts_state *s, const char *key) {
	return ucix_get_option(s->uci, s->uciname, s->profile, key);
}

static inline int umts_config_get_int
(struct umts_state *s, const char *key, int def) {
	return ucix_get_option_int(s->uci, s->uciname, s->profile, key, def);
}

static inline int umts_config_get_list
(struct umts_state *s, const char *key, struct list_head *list) {
	return ucix_get_option_list(s->uci, s->uciname, s->profile, key, list);
}

static inline void umts_config_revert(struct umts_state *s, const char *key) {
	ucix_revert(s->uci, s->uciname, s->profile, key);
}

static inline void umts_config_set(struct umts_state *s, const char *key, const char *val) {
	ucix_add_option(s->uci, s->uciname, s->profile, key, val);
}

static inline void umts_config_set_int(struct umts_state *s, const char *key, int val) {
	ucix_add_option_int(s->uci, s->uciname, s->profile, key, val);
}

static inline void umts_config_append(struct umts_state *s, const char *key, const char *val) {
	ucix_add_list_single(s->uci, s->uciname, s->profile, key, val);
}

#endif /* UMTS_CONFIG_H_ */
