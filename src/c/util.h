#pragma once

// Remove an incomplete UTF-8 character left at the end of a truncated string.
void utf8_trim_partial(char *str);
