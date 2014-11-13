/** \file */
#include "tea.h"
#define apop_strcmp(a, b) (((a)&&(b) && !strcmp((a), (b))) || (!(a) && !(b)))

typedef struct{
    char *name;
    double weight;
    int last_query;
    char type;
} used_var_t;
extern used_var_t *used_vars;

typedef struct{
    char *clause;           //the original text
    char *pre_edit;         //if of the form (if=>then), this is the then; else NULL.
    used_var_t *vars_used;  //the list of variables used
    int var_ct;             //the length of that vars_used matrix
} edit_t;
extern edit_t *edit_list;
extern int query_ct;

void write_edit_candidate(int id, int col, int val);

extern apop_data *edit_grid; //generated by bridge.c; useed by consistency_check (and elsewhere).
extern edit_t **edit_grid_to_list; //one for each row of edit_grid, pointing to source edit in edit_list.

int *find_b, *find_e, *optionct, verbose, nflds, edit_ct, total_var_ct;

void xprintf(char **q, char *format, ...); //impute/parse_sql.c
#define XN(in) ((in) ? (in) : "")  //same.

void begin_transaction();
void commit_transaction();

int using_r; //r_init handles this. If zero, then it's a standalone C library.

apop_data * get_variables_to_impute(char *tag); //impute/impute.c
int join_tables(); //text_in/text_in.c

void reset_ri_ext_table();  //c/discrete/name_conversions.c
int ri_from_ext(char const *varname, char const* ext_val);
char * ext_from_ri(char const *varname, int const ri_val);
double find_nearest_val(char const *varname, double ext_val);

char get_coltype(char const* depvar); //bridge.c

void do_recodes(); //c/text_in/recodes.c.


int check_levenshtein_distances(int);//c/peptalk/levendistance.c
void test_levenshtein_distance(); //also in levendistance.c, for the test suite.
int get_num_typos(); //c/discrete/bridge.c -- used for testing levenshtein stuff

void levenshtein_tests(); //found in tea/c/levenshtein_tests.c -- called in asst_tests.c
void write_a_file(char *, char *); //found in tea/c/tests/asst_tests.c

void pastein_tests(); //found in tea/c/pastein_tests.c -- called in asst_tests.c

void test_check_out_impute();//in checkout.c

int has_sqlite3_index(char const *table, char const *column, char);//text_in/text_in.c
