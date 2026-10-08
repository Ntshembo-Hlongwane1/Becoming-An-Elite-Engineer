# Decisions (one line each: decision — why — source)

## page_base / page_offset (mask vs modulo)

## pages_spanned (why page_base(addr+len-1) - page_base(addr) + 1, and the len==0 case)

## parse_status_kb (how you anchor the key so "RSS" doesn't match "VmRSS")

## PageProbe (mmap flags you used; why fresh pages are not resident)

## mincore interpretation (which bit means resident)
