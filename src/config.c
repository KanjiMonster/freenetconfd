/*
 * Copyright (C) 2014 Sartura, Ltd.
 * Copyright (C) 2014 Cisco Systems, Inc.
 *
 * Author: Luka Perkov <luka.perkov@sartura.hr>
 * Author: Petar Koretic <petar.koretic@sartura.hr>
 *
 * freenetconfd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with freenetconfd. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <uci.h>
#include <string.h>
#include <libubox/blobmsg.h>
#include <uci_blob.h>

#include <libgen.h>
#include <sys/stat.h>

#include "config.h"
#include "freenetconfd.h"

enum {
	ADDR,
	PORT,
	USERNAME,
	PASSWORD,
	HOST_ECDSA_KEY,
	HOST_DSA_KEY,
	HOST_RSA_KEY,
	AUTHORIZED_KEYS_FILE,
	LOG_LEVEL,
	SSH_TIMEOUT_SOCKET,
	SSH_TIMEOUT_READ,
	SSH_PCAP_ENABLE,
	SSH_PCAP_FILE,
	YANG_DIR,
	__OPTIONS_COUNT
};

const struct blobmsg_policy config_policy[__OPTIONS_COUNT] = {
	[ADDR] = { .name = "addr", .type = BLOBMSG_TYPE_STRING },
	[PORT] = { .name = "port", .type = BLOBMSG_TYPE_STRING },
	[USERNAME] = { .name = "username", .type = BLOBMSG_TYPE_STRING },
	[PASSWORD] = { .name = "password", .type = BLOBMSG_TYPE_STRING },
	[HOST_ECDSA_KEY] = { .name = "host_ecdsa_key", .type = BLOBMSG_TYPE_STRING },
	[HOST_DSA_KEY] = { .name = "host_dsa_key", .type = BLOBMSG_TYPE_STRING },
	[HOST_RSA_KEY] = { .name = "host_rsa_key", .type = BLOBMSG_TYPE_STRING },
	[AUTHORIZED_KEYS_FILE] = { .name = "authorized_keys_file", .type = BLOBMSG_TYPE_STRING },
	[LOG_LEVEL] = { .name = "log_level", .type = BLOBMSG_TYPE_INT32 },
	[SSH_TIMEOUT_SOCKET] = { .name = "ssh_timeout_socket", .type = BLOBMSG_TYPE_INT32 },
	[SSH_TIMEOUT_READ] = { .name = "ssh_timeout_read", .type = BLOBMSG_TYPE_INT32 },
	[SSH_PCAP_ENABLE] = { .name = "ssh_pcap_enable", .type = BLOBMSG_TYPE_BOOL },
	[SSH_PCAP_FILE] = { .name = "ssh_pcap_file", .type = BLOBMSG_TYPE_STRING },
	[YANG_DIR] = { .name = "yang_dir", .type = BLOBMSG_TYPE_STRING },
};
const struct uci_blob_param_list config_attr_list = {
	.n_params = __OPTIONS_COUNT,
	.params = config_policy
};

/*
 * create_dir_from_path() - create directory from file path
 *
 * @char*:  path from which directory is created (ex: "/path/to/file")
 *
 * Returns 0 if created.
 */
static int create_dir_from_path(const char *file_path)
{
	struct stat st;
	int rc;

	char *path = strdup(file_path);
	char *dir_path = dirname(path);

	if (!dir_path || !strcmp(dir_path, ".")) {
		ERROR("invalid dir path\n");
		rc = 1;
		goto exit;
	}

	rc = stat(dir_path, &st);
	if (!rc) goto exit;

	rc = mkdir(dir_path, 0700);
	if (rc) {
		ERROR("creating directory '%s' failed: %s\n", dir_path, strerror(errno));
		goto exit;
	}

exit:
	free(path);

	return rc;
}

/*
 * config_load() - load uci config file
 *
 * Load and parse uci config file. If config file is found, parse configs to
 * internal structure defined in config.h.
 */
int config_load(void)
{
	struct uci_context *uci = uci_alloc_context();
	struct uci_package *conf = NULL;
	struct blob_attr *tb[__OPTIONS_COUNT], *c;
	static struct blob_buf buf;

	if (uci_load(uci, "freenetconfd", &conf)) {
		uci_free_context(uci);
		return -1;
	}

	blob_buf_init(&buf, 0);

	struct uci_element *section_elem;
	uci_foreach_element(&conf->sections, section_elem) {
		struct uci_section *s = uci_to_section(section_elem);
		uci_to_blob(&buf, s, &config_attr_list);
	}

	blobmsg_parse(config_policy, __OPTIONS_COUNT, tb, blob_data(buf.head), blob_len(buf.head));

	/* defaults */
	config.addr = NULL;
	config.port = NULL;
	config.username = NULL;
	config.password = NULL;
	config.host_ecdsa_key = NULL;
	config.host_dsa_key = NULL;
	config.host_rsa_key = NULL;
	config.authorized_keys_file = NULL;
	config.ssh_timeout_socket = 3;
	config.ssh_timeout_read = 1000;
	config.ssh_pcap_enable = 0;
	config.ssh_pcap_file = NULL;
	config.log_level = 0;
	config.yang_dir = NULL;

	if ((c = tb[ADDR]))
		config.addr = strdup(blobmsg_get_string(c));

	if ((c = tb[PORT]))
		config.port = strdup(blobmsg_get_string(c));

	if ((c = tb[USERNAME]))
		config.username = strdup(blobmsg_get_string(c));

	if ((c = tb[PASSWORD]))
		config.password = strdup(blobmsg_get_string(c));

	if ((c = tb[HOST_ECDSA_KEY])) {
		config.host_ecdsa_key = strdup(blobmsg_get_string(c));
		create_dir_from_path(config.host_ecdsa_key);
	}

	if ((c = tb[HOST_DSA_KEY])) {
		config.host_dsa_key = strdup(blobmsg_get_string(c));
		create_dir_from_path(config.host_dsa_key);
	}

	if ((c = tb[HOST_RSA_KEY])){
		config.host_rsa_key = strdup(blobmsg_get_string(c));
		create_dir_from_path(config.host_rsa_key);
	}

	if (!(config.host_ecdsa_key || config.host_dsa_key || config.host_rsa_key)) {
		ERROR("at least one host key must be set\n");
		uci_unload(uci, conf);
		uci_free_context(uci);
		return -1;
	}

	if ((c = tb[AUTHORIZED_KEYS_FILE]))
		config.authorized_keys_file = strdup(blobmsg_get_string(c));

	if ((c = tb[SSH_TIMEOUT_SOCKET]))
		config.ssh_timeout_socket = blobmsg_get_u32(c);

	if ((c = tb[SSH_TIMEOUT_READ]))
		config.ssh_timeout_read = blobmsg_get_u32(c) * 1000;

	if ((c = tb[SSH_PCAP_ENABLE]))
		config.ssh_pcap_enable = blobmsg_get_bool(c);

	if (config.ssh_pcap_enable
	    && (c = tb[SSH_PCAP_FILE])) {
		free(config.ssh_pcap_file);
		config.ssh_pcap_file = strdup(blobmsg_get_string(c));
	}

	if ((c = tb[LOG_LEVEL]))
		config.log_level = blobmsg_get_u32(c);

	if ((c = tb[YANG_DIR]))
		config.yang_dir = strdup(blobmsg_get_string(c));

	blob_buf_free(&buf);
	uci_unload(uci, conf);
	uci_free_context(uci);

	return 0;
}

void config_exit(void)
{
	free(config.addr);
	free(config.port);
	free(config.username);
	free(config.password);
	free(config.host_dsa_key);
	free(config.host_rsa_key);
	free(config.host_ecdsa_key);
	free(config.authorized_keys_file);
	free(config.ssh_pcap_file);
	free(config.yang_dir);
}
