/*
 * error.c — Centralized error handling with structured error responses
 */

#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "project_config.h"
#include "transport.h"
#include "response.h"

static const struct {
	const char *code;
	const char *status_text;
} error_table[] = {
	// 1xx – Informational
	[100] = { "CONTINUE",                         "Continue"                         },
	[101] = { "SWITCHING_PROTOCOLS",              "Switching Protocols"              },
	[102] = { "PROCESSING",                       "Processing"                       },
	[103] = { "EARLY_HINTS",                      "Early Hints"                      },

	// 2xx – Success
	[200] = { "OK",                               "OK"                               },
	[201] = { "CREATED",                          "Created"                          },
	[202] = { "ACCEPTED",                         "Accepted"                         },
	[203] = { "NON_AUTHORITATIVE_INFORMATION",    "Non-Authoritative Information"    },
	[204] = { "NO_CONTENT",                       "No Content"                       },
	[205] = { "RESET_CONTENT",                    "Reset Content"                    },
	[206] = { "PARTIAL_CONTENT",                  "Partial Content"                  },
	[207] = { "MULTI_STATUS",                     "Multi-Status"                     },
	[208] = { "ALREADY_REPORTED",                 "Already Reported"                 },
	[226] = { "IM_USED",                          "IM Used"                          },

	// 3xx – Redirection
	[301] = { "MOVED_PERMANENTLY",                "Moved Permanently"                },
	[302] = { "FOUND",                            "Found"                            },
	[303] = { "SEE_OTHER",                        "See Other"                        },
	[300] = { "MULTIPLE_CHOICES",                 "Multiple Choices"                 },
	[304] = { "NOT_MODIFIED",                     "Not Modified"                     },
	[305] = { "USE_PROXY",                        "Use Proxy"                        },
	[307] = { "TEMPORARY_REDIRECT",               "Temporary Redirect"               },
	[308] = { "PERMANENT_REDIRECT",               "Permanent Redirect"               },

	// 4xx – Client Errors
	[400] = { "BAD_REQUEST",                      "Bad Request"                      },
	[401] = { "UNAUTHORIZED",                     "Unauthorized"                     },
	[402] = { "PAYMENT_REQUIRED",                 "Payment Required"                 },
	[403] = { "FORBIDDEN",                        "Forbidden"                        },
	[404] = { "NOT_FOUND",                        "Not Found"                        },
	[405] = { "METHOD_NOT_ALLOWED",               "Method Not Allowed"               },
	[406] = { "NOT_ACCEPTABLE",                   "Not Acceptable"                   },
	[407] = { "PROXY_AUTHENTICATION_REQUIRED",    "Proxy Authentication Required"    },
	[408] = { "REQUEST_TIMEOUT",                  "Request Timeout"                  },
	[409] = { "CONFLICT",                         "Conflict"                         },
	[410] = { "GONE",                             "Gone"                             },
	[411] = { "LENGTH_REQUIRED",                  "Length Required"                  },
	[412] = { "PRECONDITION_FAILED",              "Precondition Failed"              },
	[413] = { "PAYLOAD_TOO_LARGE",                "Payload Too Large"                },
	[414] = { "URI_TOO_LONG",                     "URI Too Long"                     },
	[415] = { "UNSUPPORTED_MEDIA_TYPE",           "Unsupported Media Type"           },
	[416] = { "RANGE_NOT_SATISFIABLE",            "Range Not Satisfiable"            },
	[417] = { "EXPECTATION_FAILED",               "Expectation Failed"               },
	[418] = { "IM_A_TEAPOT",                      "I'm a Teapot"                     },
	[421] = { "MISDIRECTED_REQUEST",              "Misdirected Request"              },
	[422] = { "UNPROCESSABLE_CONTENT",            "Unprocessable Content"            },
	[423] = { "LOCKED",                           "Locked"                           },
	[424] = { "FAILED_DEPENDENCY",                "Failed Dependency"                },
	[425] = { "TOO_EARLY",                        "Too Early"                        },
	[426] = { "UPGRADE_REQUIRED",                 "Upgrade Required"                 },
	[428] = { "PRECONDITION_REQUIRED",            "Precondition Required"            },
	[429] = { "TOO_MANY_REQUESTS",                "Too Many Requests"                },
	[431] = { "REQUEST_HEADER_FIELDS_TOO_LARGE",  "Request Header Fields Too Large"  },
	[451] = { "UNAVAILABLE_FOR_LEGAL_REASONS",    "Unavailable For Legal Reasons"    },

	// 5xx – Server Errors
	[500] = { "INTERNAL_SERVER_ERROR",            "Internal Server Error"            },
	[501] = { "NOT_IMPLEMENTED",                  "Not Implemented"                  },
	[502] = { "BAD_GATEWAY",                      "Bad Gateway"                      },
	[503] = { "SERVICE_UNAVAILABLE",              "Service Unavailable"              },
	[504] = { "GATEWAY_TIMEOUT",                  "Gateway Timeout"                  },
	[505] = { "HTTP_VERSION_NOT_SUPPORTED",       "HTTP Version Not Supported"       },
	[506] = { "VARIANT_ALSO_NEGOTIATES",          "Variant Also Negotiates"          },
	[507] = { "INSUFFICIENT_STORAGE",             "Insufficient Storage"             },
	[508] = { "LOOP_DETECTED",                    "Loop Detected"                    },
	[510] = { "NOT_EXTENDED",                     "Not Extended"                     },
	[511] = { "NETWORK_AUTHENTICATION_REQUIRED",  "Network Authentication Required"  }
};

const char *error_find_code(int status)
{
	if (status >= 100 && status <= 511 && error_table[status].code)
		return error_table[status].code;
	return "INTERNAL_SERVER_ERROR";
}

const char *error_find_status_text(int status)
{
	if (status >= 100 && status <= 511 && error_table[status].status_text)
		return error_table[status].status_text;
	return "Internal Server Error";
}
