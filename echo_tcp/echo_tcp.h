#pragma once

#define SERVICE "52674"
#define MAX_REQ_LEN 512
#define APPENDIX "3.14159"
#define MAX_RESP_LEN (MAX_REQ_LEN + strlen(APPENDIX) + 1) // + 1 for newline character
