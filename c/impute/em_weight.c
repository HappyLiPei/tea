#include "em_weight.h"

//cut/pasted from Apophenia/model/apop_pmf.c. It should be an apop_data_rowcmp function.
static int are_equal(apop_data const *left, apop_data const *right){
    if (left->vector){
        if (!right->vector ||
              (*left->vector->data != *right->vector->data
               && !(gsl_isnan(*left->vector->data) && gsl_isnan(*right->vector->data))))
            return 0;
    } else if (right->vector) return 0;

    if (left->matrix){
        if (!right->matrix ||
              left->matrix->size2 != right->matrix->size2) return 0;
        for (int i=0; i< left->matrix->size2; i++){
            double L = gsl_matrix_get(left->matrix, 0, i);
            double R = gsl_matrix_get(right->matrix, 0, i);
            if (L != R && !(gsl_isnan(L) && gsl_isnan(R))) return 0;
        }
    }
    else if (right->matrix) return 0;

    if (left->textsize[1]){
        if (left->textsize[1] != right->textsize[1]) return 0;
        for (int i=0; i< left->textsize[1]; i++)
            if (strcmp(left->text[0][i], right->text[0][i])) return 0;
    }
    else if (right->textsize[1]) return 0;
    return 1;
}

int weightless(apop_data *onerow, void *extra_param){ return gsl_vector_get(onerow->weights, 0)==0; }

//This is a substitute for apop_pmf_compress, because
//we can use knowledge of our special case to work more efficiently
void merge_two_sets(apop_data *left, apop_data *right){
    for  (int i=0; i< right->matrix->size1; i++) {
        Apop_row(right, i, Rrow);
        double *r = gsl_vector_ptr(Rrow->weights, 0);
        if (!*r) continue;
        int j;
        bool done = false;
        #pragma omp parallel for private(j) shared(done)
        for (j=0; j< left->matrix->size1; j++){
            Apop_row(left, j, Lrow);
            if (are_equal(Rrow, Lrow)){
                *gsl_vector_ptr(Lrow->weights, 0) += *r;
                *r = 0;
                done = true;
                if (done) j = left->matrix->size1;
            }
        }
    }
    apop_data_rm_rows(right, .do_drop=weightless);
    //apop_data_listwise_delete(left, .inplace='y');
    apop_data_stack(left, right, .inplace='y');
}

// Zero out the weights of those rows that don't match.
// Rescale the weights of those rows that are near-misses.
static double cull2(apop_data const *onerow, apop_data *cullback){
    if (!cullback) return 0;
    #pragma omp parallel for
    for (int row=0; row<cullback->matrix->size1; row++){
        Apop_row(cullback, row, cull_row);
        if (!*cull_row->weights->data) continue;
        for (int i=0; i< cull_row->matrix->size2; i++){
            double this= onerow->matrix->data[i];
            if (isnan(this)) continue;
            double crthis= cull_row->matrix->data[i];
            if (onerow->more && onerow->more->text[i][0][0]=='r'){//near-misses OK.
                double dist = fabs(this - crthis);
                *cull_row->weights->data *= 1/(1+dist);
            }
            else if (crthis != this) {
                *cull_row->weights->data= 0;
                break;
            }
        }
    }
    return 0;
}


/* In this version, both the reference row and the weight set to be culled
   may have NaNs. We still require compabibility in those fields where both
   have data, but where one has a NaN and the other doesn't, we write down 
   the nonmissing value for that field, regardless of which side it came from.
   Therefore, the resultant data has fewer NaN fields than either source, and 
   repeating this over several iterations can eventually produce a NaN-free set.

   Not all data sets can complete like this.

   If a row has NaNs, but there is no additional fill-in, then give it zero weight.

   The rules:
--If a row has any NaN data, skip self in the cullback.
--If there is another row with unambiguously more data, skip this row.
--As an elaboration, if there is complete data anywhere in the candidate set, ignore
any incomplete rows (even if they are complete after fillin).

So, I need two passes:
(1) mark whether the row has NaNs.
(2) mark whether any admissable row has no NaNs.
(3) Check whether the row has any fill-ins. If has NaNs but no fill-ins, then it is either self or has even less data.
2nd pass:
(4) If any admissable rows have no NaNs, zero out all previously admissable 
rows with NaNs.
*/
static double cull_w_nans(apop_data const *onerow, apop_data *cullback){
    if (!cullback || !cullback->matrix) return 0;
    bool has_nans[cullback->matrix->size1];
    bool complete_admissable_row = false;
    for (int row=0; row<cullback->matrix->size1; row++){
        Apop_row(cullback, row, cull_row);
        has_nans[row] = false;
        bool this_row_has_fillins = false;
        double *weight = gsl_vector_ptr(cullback->weights, row);
        for (int i=0; i< cull_row->matrix->size2; i++){
            double ref_field = apop_data_get(onerow, .col=i);
            double *cull_field = apop_data_ptr(cull_row, .col=i);
            has_nans[row] = has_nans[row] || isnan(*cull_field);  //step (1)
            if (!isnan(*cull_field) && !isnan(ref_field)){
                if (onerow->more && onerow->more->text[i][0][0]=='r'){//near-misses OK.
                    double dist = fabs(ref_field - *cull_field);
                    *cull_row->weights->data *= 1/(1+dist);
                    break;
                } else if ((*cull_field != ref_field) //mismatch
                        || (has_nans[row] && complete_admissable_row)) { //step (4)
                    *weight = 0;
                    break;
                }
            }
            if (isnan(*cull_field) && !isnan(ref_field)){
                *cull_field = ref_field;
                this_row_has_fillins = true;
            }
        }
        if (!has_nans[row] && *weight != 0)         //step (2)
            complete_admissable_row = true;
        if (has_nans[row] && !this_row_has_fillins) //step (3)
            *weight = 0;
    }
    if (complete_admissable_row)                    //step (4)
        for (int row=0; row<cullback->matrix->size1; row++)
            if (has_nans[row]) gsl_vector_set(cullback->weights, row, 0);
    return 0;
}

void save_candidate(apop_data *candidate, apop_data **prior_candidate){
    apop_data_free(*prior_candidate);
    *prior_candidate = candidate;
    //apop_data_pmf_compress(*prior_candidate);
}

void prep_a_copy(apop_data **cp, apop_data *prior_candidate){
    if (!*cp) *cp = apop_data_copy(prior_candidate);
    else     gsl_vector_memcpy((*cp)->weights, prior_candidate->weights);
}

void merge_in_weights_so_far(apop_data *new_bit, apop_data *main_set){
    if (!new_bit) return;
    if (new_bit->weights->size != main_set->weights->size)
        merge_two_sets(new_bit, main_set);   //shrinks main_set
    else //candidate, prior_candidate, and cp differ only by weights
        gsl_vector_add(new_bit->weights, main_set->weights);
}

void rescale_cp_and_merge_into_candidate(apop_data *candidate, apop_data **cp, double scale_to){
    if (!*cp) return;
    double cp_weight_sum = apop_sum((*cp)->weights);
    if (cp_weight_sum) {
        gsl_vector_scale((*cp)->weights, scale_to/cp_weight_sum);
        merge_in_weights_so_far(candidate, *cp);//append cp into candidate
    }
    if (!(*cp)->weights || (candidate? candidate->weights->size : 0) != (*cp)->weights->size)
        apop_data_free(*cp);
}

void clean_up_for_phase_II(apop_data *candidate, apop_data **cp){
    apop_data_listwise_delete(candidate, .inplace='y');
    apop_data_pmf_compress(candidate);
    apop_data_free(*cp);
}

/* This version uses cull_w_nans. At the bottom of the for loop,
   we have a candidate set that has fewer NaNs than before, and is weighted to the
   count of the original data.

   make one copy for the candidate weights; make another copy which is now the data
   set; rerun for as many times as there are fields, because the worst case is that
   each run reduces an observation's NaN count by one field.

   So what changed? 
   --The indata is not listwise deleted.
   --the candidate is copied to be the new data set.
   --we use a different cull function.

*/
apop_data *em_weight_base(em_weight_s in){
    apop_data *candidate = apop_data_copy(in.d);
    apop_data *clean_copy = apop_data_copy( //massive speed gain in some cases.
                                    apop_data_pmf_compress(candidate));
    apop_data *complete_copy = apop_data_listwise_delete(clean_copy);
    double tolerance = in.tolerance ? in.tolerance : 1e-5;
    apop_vector_normalize(candidate->weights);
    apop_data *prior_candidate = NULL;
    int ctr = 0;
    int saturated = (int) log2(in.d->matrix->size2);
    apop_data *cp = NULL;
    do {
        save_candidate(candidate, &prior_candidate);
        candidate = apop_data_copy(ctr>=saturated? complete_copy : clean_copy);
        for (int i=0; i < clean_copy->matrix->size1; i++) {
            Apop_row(in.d, i, row);
            if (isnan(apop_matrix_sum(row->matrix))) {
                prep_a_copy(&cp, prior_candidate);
                (ctr<saturated ? cull_w_nans : cull2)(row, cp);
                rescale_cp_and_merge_into_candidate(candidate, &cp,
                                           gsl_vector_get(row->weights, 0));
            }
        }
        if (ctr==saturated) clean_up_for_phase_II(candidate, &cp);
        if (candidate) apop_vector_normalize(candidate->weights);
    } while (ctr++<= saturated
              || (candidate && (candidate->weights->size != prior_candidate->weights->size
              || apop_vector_distance(candidate->weights, prior_candidate->weights, .metric='m') > tolerance)));
    apop_data_free(cp);
    apop_data_free(prior_candidate);
    apop_data_free(clean_copy);
    apop_data_free(complete_copy);
    //apop_data_free(complete);
    return candidate;
}
