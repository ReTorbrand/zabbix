/*
** Zabbix
** Copyright (C) 2001-2018 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "sysinfo.h"
#include "zbxregexp.h"

#include "comms.h"
#include "cfg.h"

#include "http.h"

#define ZBX_MAX_WEBPAGE_SIZE	(1 * 1024 * 1024)
static const char URI_PROHIBIT_CHARS[] = {0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xA,0xB,0xC,0xD,0xE,0xF,0x10,0x11,0x12,\
	0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x7F,0};

static int	get_http_page(const char *host, const char *path, unsigned short port, char *buffer,
		size_t max_buffer_len, char **error)
{
	int		ret;
	char		*wrong_chr;
	char		request[MAX_STRING_LEN];
	zbx_socket_t	s;

	if (NULL != (wrong_chr = strpbrk(host, URI_PROHIBIT_CHARS)))
	{
		*error = zbx_dsprintf(NULL, "Incorrect hostname expression. Check hostname part after: %.*s.",
				(int)(wrong_chr - host), host);
		return SYSINFO_RET_FAIL;
	}

	if (NULL != (wrong_chr = strpbrk(path, URI_PROHIBIT_CHARS)))
	{
		*error = zbx_dsprintf(NULL, "Incorrect path expression. Check path part after: %.*s.",
				(int)(wrong_chr - path), path);
		return SYSINFO_RET_FAIL;
	}

	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, CONFIG_TIMEOUT,
			ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
	{
		zbx_snprintf(request, sizeof(request),
				"GET %s%s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Connection: close\r\n"
				"\r\n",
				'/' != *path ? "/" : "", path, host);

		if (SUCCEED == (ret = zbx_tcp_send_raw(&s, request)))
		{
			if (SUCCEED == (ret = zbx_tcp_recv_raw(&s)))
			{
				if (NULL != buffer)
					zbx_strlcpy(buffer, s.buffer, max_buffer_len);
			}
		}

		zbx_tcp_close(&s);
	}

	if (FAIL == ret)
	{
		*error = zbx_dsprintf(NULL, "HTTP get error: %s", zbx_socket_strerror());
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	WEB_PAGE_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*hostname, *path_str, *port_str, buffer[MAX_BUFFER_LEN], path[MAX_STRING_LEN], *error;
	unsigned short	port_number;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	if (NULL == hostname || '\0' == *hostname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == path_str)
		*path = '\0';
	else
		strscpy(path, path_str);

	if (NULL == port_str || '\0' == *port_str)
		port_number = ZBX_DEFAULT_HTTP_PORT;
	else if (FAIL == is_ushort(port_str, &port_number))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK == get_http_page(hostname, path, port_number, buffer, sizeof(buffer), &error))
	{
		zbx_rtrim(buffer, "\r\n");
		SET_TEXT_RESULT(result, zbx_strdup(NULL, buffer));
	}
	else
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	WEB_PAGE_PERF(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*hostname, path[MAX_STRING_LEN], *port_str, *path_str, *error;
	double		start_time;
	unsigned short	port_number;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	if (NULL == hostname || '\0' == *hostname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == path_str || '\0' == *path_str)
		*path = '\0';
	else
		strscpy(path, path_str);

	if (NULL == port_str || '\0' == *port_str)
		port_number = ZBX_DEFAULT_HTTP_PORT;
	else if (FAIL == is_ushort(port_str, &port_number))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	start_time = zbx_time();

	if (SYSINFO_RET_OK == get_http_page(hostname, path, port_number, NULL, 0, &error))
	{
		SET_DBL_RESULT(result, zbx_time() - start_time);

	}
	else
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	WEB_PAGE_REGEXP(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*hostname, *path_str, *port_str, *regexp, *length_str, path[MAX_STRING_LEN],
			*buffer = NULL, *ptr = NULL, *str, *newline, *error;
	int		length;
	const char	*output;
	unsigned short	port_number;

	if (6 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (4 > request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);
	regexp = get_rparam(request, 3);
	length_str = get_rparam(request, 4);
	output = get_rparam(request, 5);

	if ('\0' == *hostname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if ('\0' == *path_str)
		*path = '\0';
	else
		strscpy(path, path_str);

	if ('\0' == *port_str)
		port_number = ZBX_DEFAULT_HTTP_PORT;
	else if (FAIL == is_ushort(port_str, &port_number))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == length_str || '\0' == *length_str)
		length = MAX_BUFFER_LEN - 1;
	else if (FAIL == is_uint31_1(length_str, &length))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* by default return the matched part of web page */
	if (NULL == output || '\0' == *output)
		output = "\\0";

	buffer = (char *)zbx_malloc(buffer, ZBX_MAX_WEBPAGE_SIZE);

	if (SYSINFO_RET_OK == get_http_page(hostname, path, port_number, buffer, ZBX_MAX_WEBPAGE_SIZE, &error))
	{
		for (str = buffer; ;)
		{
			if (NULL != (newline = strchr(str, '\n')))
			{
				if (str != newline && '\r' == newline[-1])
					newline[-1] = '\0';
				else
					*newline = '\0';
			}

			if (SUCCEED == zbx_regexp_sub(str, regexp, output, &ptr) && NULL != ptr)
				break;

			if (NULL != newline)
				str = newline + 1;
			else
				break;
		}
	}
	else
	{
		zbx_free(buffer);
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL != ptr)
		SET_STR_RESULT(result, ptr);
	else
		SET_STR_RESULT(result, zbx_strdup(NULL, ""));

	zbx_free(buffer);

	return SYSINFO_RET_OK;
}
