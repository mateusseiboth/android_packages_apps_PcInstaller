#include "check.h"
#include "proto.h"

static void parses_word_and_fields(void)
{
	struct msg m;
	CHECK(!parse_msg("CHECK part=sda3 folder=lineage system=image\n", &m));
	CHECK_STR(m.word, "CHECK");
	CHECK(m.count == 3);
	CHECK_STR(find_field(&m, "part"), "sda3");
	CHECK_STR(find_field(&m, "system"), "image");
	CHECK(find_field(&m, "data") == NULL);
}

static void decodes_percent_escapes_in_values(void)
{
	struct msg m;
	CHECK(!parse_msg("PART label=Meus%20Arquivos%25 x=a%3db", &m));
	CHECK_STR(find_field(&m, "label"), "Meus Arquivos%");
	CHECK_STR(find_field(&m, "x"), "a=b");
}

static void rejects_malformed_lines(void)
{
	struct msg m;
	CHECK(parse_msg("", &m));
	CHECK(parse_msg("CHECK noequals", &m));
	CHECK(parse_msg("CHECK bad=%zz", &m));
	CHECK(parse_msg("check lower=case", &m));
}

static void formats_with_escapes_and_numbers(void)
{
	struct line l;
	line_begin(&l, "PART");
	line_add(&l, "label", "Dados do PC");
	line_add_u64(&l, "size", 512ULL << 30);
	line_add(&l, "empty", "");
	CHECK_STR(line_end(&l), "PART label=Dados%20do%20PC size=549755813888 empty=\n");
}

static void a_formatted_line_parses_back(void)
{
	struct line l;
	struct msg m;
	line_begin(&l, "ERR");
	line_add(&l, "detail", "a=b c\n%d");
	CHECK(!parse_msg(line_end(&l), &m));
	CHECK_STR(find_field(&m, "detail"), "a=b c\n%d");
}

static void reads_unsigned_fields(void)
{
	struct msg m;
	unsigned long long v = 0;
	CHECK(!parse_msg("CHECK data_size=8589934592 bad=12x", &m));
	CHECK(!find_u64(&m, "data_size", &v) && v == 8589934592ULL);
	CHECK(find_u64(&m, "bad", &v));
	CHECK(find_u64(&m, "missing", &v));
}

int main(void)
{
	RUN(parses_word_and_fields);
	RUN(decodes_percent_escapes_in_values);
	RUN(rejects_malformed_lines);
	RUN(formats_with_escapes_and_numbers);
	RUN(a_formatted_line_parses_back);
	RUN(reads_unsigned_fields);
	return CHECK_DONE();
}
