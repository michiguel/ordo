/*
	Ordo is program for calculating ratings of engine or chess players
    Copyright 2013 Miguel A. Ballicora

    This file is part of Ordo.

    Ordo is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ordo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ordo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stddef.h>
#include <assert.h>

#include "mytypes.h"
#include "report.h"
#include "encount.h"
#include "cegt.h"
#include "string.h"
#include "gauss.h"
#include "math.h"
#include "ordolim.h"
#include "xpect.h"
#include "mymem.h"

//=== duplicated in main.c =========

static ptrdiff_t
head2head_idx_sdev (ptrdiff_t x, ptrdiff_t y)
{	
	ptrdiff_t idx;
	if (y < x) 
		idx = (x*x-x)/2+y;					
	else
		idx = (y*y-y)/2+x;
	return idx;
}

//==================================

static void
calc_encounters__
				( int selectivity
				, const struct GAMES *g
				, const bool_t *flagged
				, struct ENCOUNTERS	*e
) 
{
	e->n = calc_encounters	
					( selectivity
					, g
					, flagged
					, e->enc);
}

static int
compare__ (const player_t *a, const player_t *b, const double *reference )
{	
	const player_t *ja = a;
	const player_t *jb = b;
	const double *r = reference;

	const double da = r[*ja];
	const double db = r[*jb];

	return (da < db) - (da > db);
}

static void
insertion_sort (const double *reference, size_t n, player_t *vect)
{
	size_t i, j, pivot;
	player_t tmp;
	if (n < 2) return;
	for (j = n-1; j > 0; j--) {
		pivot = j - 1;
		for (i = j; i < n; i++) {
			if (!(0 < compare__(&vect[pivot], &vect[i], reference))) {
				break;
			}
		}
		if (i > j) {
			size_t k;
			tmp = vect[j-1];
			for (k = j; k < i; k++) {
				vect[k-1] = vect[k];
			}
			vect[i-1] = tmp;
		}
	}
}

//==== my qsort 

static size_t
partition (const double *reference, player_t *vect, size_t l, size_t h)
{
	size_t p;
	size_t i = l + 1;
	player_t tmp;
	player_t pivot_content = vect[l];

	while (i < h) {
		if (0 < compare__(&pivot_content, &vect[i], reference)) {
			i++;
		} else {
			tmp = vect[i]; vect[i] = vect[h]; vect[h] = tmp; // swap i, h
			h--;
		}
	}

	p = 0 < compare__(&pivot_content, &vect[i], reference)? i: i - 1;
	
	if (p > l) 	{
			tmp = vect[p]; vect[p] = vect[l]; vect[l] = tmp; // swap p, l
	}
	return p;
}

static void
range_qsort (const double *reference, player_t *vect, size_t l, size_t h)
{

	size_t m;
	assert (h >= l);

	if (h < l + 6) {
		// could be here:	insertion_sort (reference, h - l + 1, vect + l);
		// left unsorted and run a full insertion sort at the end.
		return;
	}

	// choose pivot and place it in --> l
	if (h <= l + 12) {
		player_t tmp;
		size_t j = l + (h-l)/2;

		if (0 < compare__(&vect[h], &vect[j], reference) && 0 < compare__(&vect[j], &vect[l], reference)) {
			tmp = vect[j]; vect[j] = vect[l]; vect[l] = tmp; // swap
		}
		if (0 < compare__(&vect[j], &vect[h], reference) && 0 < compare__(&vect[h], &vect[l], reference)) {
			tmp = vect[h]; vect[h] = vect[l]; vect[l] = tmp; // swap
		}

	}

	m = partition (reference, vect, l, h);

	if (m-l < h-m) { // do smallest range first, prevents stack problems
		if (m > l+1) range_qsort(reference, vect, l, m-1);
		if (h > m+1) range_qsort(reference, vect, m+1, h);
	} else {
		if (h > m+1) range_qsort(reference, vect, m+1, h);
		if (m > l+1) range_qsort(reference, vect, l, m-1);
	}
}

static void
my_qsort (const double *reference, size_t n, player_t *vect)
{
	range_qsort(reference, vect, 0, n-1);
	insertion_sort (reference, n, vect); // only if it was not sorted inside range_qsort
}

//==== end my qsort

static size_t
find_maxlen (const char *nm[], size_t n)
{
	size_t maxl = 0;
	size_t length;
	size_t i;
	for (i = 0; i < n; i++) {
		length = strlen(nm[i]);
		if (length > maxl) maxl = length;
	}
	return maxl;
}


#define MAXSYMBOLS_STR 5
static const char *SP_symbolstr[MAXSYMBOLS_STR] = {"<",">","*"," ","X"};

static const char *
get_super_player_symbolstr(player_t j, const struct PLAYERS *pPlayers)
{
	assert(pPlayers->perf_set);
	if (pPlayers->performance_type[j] == PERF_SUPERLOSER) {
		return SP_symbolstr[0];
	} else if (pPlayers->performance_type[j] == PERF_SUPERWINNER) {
		return SP_symbolstr[1];
	} else if (pPlayers->performance_type[j] == PERF_NOGAMES) {
		return SP_symbolstr[2];
	} else if (pPlayers->performance_type[j] == PERF_NORMAL) {
		return SP_symbolstr[3];
	} else
		return SP_symbolstr[4];
}

static bool_t
is_old_version(player_t j, const struct rel_prior_set *rps)
{
	player_t i;
	bool_t found;
	player_t rn = rps->n;
	const struct relprior *rx = rps->x;
	for (i = 0, found = FALSE; !found && i < rn; i++) {
		found = j == rx[i].player_b;
	}
	return found;
}

static double
rating_round(double x, int d)
{
	const int al[6] = {1,10,100,1000,10000,100000};
	int i;
	double y;
	if (d > 5) d = 5;
	if (d < 0) d = 0;
	y = x * al[d] + 0.5; 
	i = (int) floor(y);
	return (double)i/al[d];
}

#define NOSDEV "  ----"

static char *
get_sdev_str (double sdev, double confidence_factor, char *str, int decimals)
{
	double x = sdev * confidence_factor;
	if (sdev > 0.00000001) {
		sprintf(str, "%6.*f", decimals, rating_round(x, decimals));
	} else {
		sprintf(str, "%s", NOSDEV);
	}
	return str;
}


static bool_t
ok_to_out (player_t j, const struct output_qualifiers *poutqual, const struct PLAYERS *p, const struct RATINGS *r)
{
	gamesnum_t games = r->playedby_results[j];
	bool_t ok = !p->flagged[j]
				&& games > 0
				&& (!poutqual->mingames_set || games >= poutqual->mingames);
	return ok;
} 

//======================

void 
cegt_output	( bool_t quiet
			, const struct GAMES 	*g
			, const struct PLAYERS 	*p
			, const struct RATINGS 	*r
			, struct ENCOUNTERS 	*e  // memory just provided for local calculations
			, double 				*sdev
			, long 					simulate
			, double				confidence_factor
			, const struct GAMESTATS *pgame_stats
			, const struct DEVIATION_ACC *s
			, struct output_qualifiers outqual)
{
	struct CEGT cegt;
	player_t j;

	assert (g);
	assert (p);
	assert (r);
	assert (e);
	assert (pgame_stats);

	calc_encounters__(ENCOUNTERS_NOFLAGGED, g, p->flagged, e);
	calc_obtained_playedby(e->enc, e->n, p->n, r->obtained, r->playedby);
	for (j = 0; j < p->n; j++) {
		r->sorted[j] = j;
	}

	my_qsort(r->ratingof_results, (size_t)p->n, r->sorted);

	cegt.n_enc = e->n; 
	cegt.enc = e->enc;
	cegt.simulate = simulate;
	cegt.n_players = p->n;
	cegt.sorted = r->sorted;
	cegt.ratingof_results = r->ratingof_results;
	cegt.obtained_results = r->obtained_results;
	cegt.playedby_results = r->playedby_results;
	cegt.sdev = sdev; 
	cegt.flagged = p->flagged;
	cegt.name = p->name;
	cegt.confidence_factor = confidence_factor;

	cegt.gstat = pgame_stats;

	cegt.sim = s;

	cegt.outqual = outqual;

	output_cegt_style (quiet, "general.dat", "rating.dat", "programs.dat", &cegt);
}


static const char *Player_str = "PLAYER";
static const char *Rating_str = "RATING";
static const char *Error_str  = "ERROR";
static const char *Points_str = "POINTS";
static const char *Played_str = "PLAYED";
static const char *Percent_str = "(%)";

// Function provided to have all head to head information

void 
head2head_output( const struct GAMES 	*		g
				, const struct PLAYERS 	*		p
				, const struct RATINGS 	*		r
				, struct ENCOUNTERS 	*		e  // memory just provided for local calculations
				, double 				*		sdev
				, long 							simulate
				, double						confidence_factor
				, const struct GAMESTATS *		pgame_stats
				, const struct DEVIATION_ACC *	s
				, const char *					head2head_str
				, int 							decimals)
{
	struct CEGT cegt;
	player_t j;

	assert (g);
	assert (p);
	assert (r);
	assert (e);
	assert (pgame_stats);
	assert (s);

	calc_encounters__(ENCOUNTERS_NOFLAGGED, g, p->flagged, e);
	calc_obtained_playedby(e->enc, e->n, p->n, r->obtained, r->playedby);
	for (j = 0; j < p->n; j++) {
		r->sorted[j] = j; 
	}

	my_qsort(r->ratingof_results, (size_t)p->n, r->sorted);

	cegt.n_enc = e->n;
	cegt.enc = e->enc;
	cegt.simulate = simulate;
	cegt.n_players = p->n;
	cegt.sorted = r->sorted;
	cegt.ratingof_results = r->ratingof_results;
	cegt.obtained_results = r->obtained_results;
	cegt.playedby_results = r->playedby_results;
	cegt.sdev = sdev; 
	cegt.flagged = p->flagged;
	cegt.name = p->name;
	cegt.confidence_factor = confidence_factor;

	cegt.gstat = pgame_stats;

	cegt.sim = s;

	cegt.decimals = decimals;
	output_report_individual (head2head_str, &cegt, (int)simulate);
}

#ifndef NDEBUG
static bool_t 
is_empty_player(player_t j, const struct PLAYERS *pPlayers)
{
	assert(pPlayers->perf_set);
	return pPlayers->performance_type[j] == PERF_NOGAMES
	;		
}
#endif

void
all_report 	( const struct GAMES 	*g
			, const struct PLAYERS 	*p
			, const struct RATINGS 	*r
			, const struct rel_prior_set *rps
			, struct ENCOUNTERS 	*e  // memory just provided for local calculations
			, double 				*sdev
			, long 					simulate
			, bool_t				hide_old_ver
			, double				confidence_factor
			, FILE 					*csvf
			, FILE 					*textf
			, double 				white_advantage
			, double 				drawrate_evenmatch
			, int					decimals
			, struct output_qualifiers	outqual
			, double				wa_sdev				
			, double				dr_sdev
			)
{
	FILE *f;
	player_t i;
	player_t j;
	size_t ml;
	char sdev_str_buffer[80];
	const char *sdev_str;
	int rank = 0;
	bool_t showrank = TRUE;

	assert (g);
	assert (p);
	assert (r);
	assert (e);
	assert (rps);

	calc_encounters__(ENCOUNTERS_NOFLAGGED, g, p->flagged, e);

	calc_obtained_playedby(e->enc, e->n, p->n, r->obtained, r->playedby);

	for (j = 0; j < p->n; j++) {
		r->sorted[j] = j; 
	}

	my_qsort(r->ratingof_results, (size_t)p->n, r->sorted);

	/* output in text format */
	f = textf;
	if (f != NULL) {

		ml = find_maxlen (p->name, (size_t)p->n);
		if (ml > 50) ml = 50;
		if (ml < strlen(Player_str)) ml = strlen(Player_str);

		if (simulate < 2) {
			fprintf(f, "\n%s %-*s    :%7s %9s %7s %6s\n", 
				"   #", 			
				(int)ml,
				Player_str, Rating_str, Points_str, Played_str, Percent_str);
	
			for (i = 0; i < p->n; i++) {

				j = r->sorted[i]; 

				if (ok_to_out (j, &outqual, p, r)) {

					char rankbuf[80];
					showrank = !is_old_version(j, rps); 
					if (showrank) {
						rank++;
						sprintf(rankbuf,"%d",rank);
					} else {
						rankbuf[0] = '\0';
					}

					if (showrank
						|| !hide_old_ver
					){
						fprintf(f, "%4s %-*s %s :%7.*f %9.1f %7ld %6.1f%s\n", 
							rankbuf,
							(int)ml+1,
							p->name[j],
							get_super_player_symbolstr(j,p),
							decimals,
							rating_round (r->ratingof_results[j], decimals), 
							r->obtained_results[j], 
							(long)r->playedby_results[j], 
							r->playedby_results[j]==0? 0: 100.0*r->obtained_results[j]/(double)r->playedby_results[j], 
							"%"
						);
					}
				} 
			}

		} else {
			fprintf(f, "\n%s %-*s    :%7s %6s %8s %7s %6s\n", 
				"   #", 
				(int)ml, 
				Player_str, Rating_str, Error_str, Points_str, Played_str, Percent_str);
	
			for (i = 0; i < p->n; i++) {
				j = r->sorted[i]; 

				sdev_str = sdev? get_sdev_str (sdev[j], confidence_factor, sdev_str_buffer, decimals): NOSDEV;

				assert(r->playedby_results[j] != 0 || is_empty_player(j,p));

				if (ok_to_out (j, &outqual, p, r)) {

					char rankbuf[80];
					showrank = !is_old_version(j, rps);
					if (showrank) {
						rank++;
						sprintf(rankbuf,"%d",rank);
					} else {
						rankbuf[0] = '\0';
					}

					if (showrank
						|| !hide_old_ver
					){
						fprintf(f, "%4s %-*s %s :%7.*f %s %8.1f %7ld %6.1f%s\n", 
						rankbuf,
						(int)ml+1, 
						p->name[j],
						get_super_player_symbolstr(j,p),
						decimals,
						rating_round(r->ratingof_results[j], decimals), 
						sdev_str, 
						r->obtained_results[j], 
						(long)r->playedby_results[j], 
						r->playedby_results[j]==0?0:100.0*r->obtained_results[j]/(double)r->playedby_results[j], 
						"%"
						);
					}
				}
			}
		}

		if (simulate < 2) {
			fprintf (f,"\n");
			fprintf (f,"White advantage = %.2f\n", white_advantage);
			fprintf (f,"Draw rate (equal opponents) = %.2f %s\n",100*drawrate_evenmatch, "%");
			fprintf (f,"\n");
		} else {
			fprintf (f,"\n");
			fprintf (f,"White advantage = %.2f +/- %.2f\n",white_advantage, wa_sdev);
			fprintf (f,"Draw rate (equal opponents) = %.2f %s +/- %.2f\n",100*drawrate_evenmatch, "%", 100*dr_sdev);
			fprintf (f,"\n");
		}

	} /*if*/

	/* output in a comma separated value file */
	f = csvf;
	if (f != NULL) {
			fprintf(f, "\"%s\""
			",\"%s\""
			",\"%s\""
			",\"%s\""
			",\"%s\""
			",\"%s\""
			",%s"
			"\n"	
			,"#"	
			,Player_str
			,Rating_str 
			,Error_str
			,Points_str
			,Played_str
			,Percent_str
			);
		rank = 0;
		for (i = 0; i < p->n; i++) {
			j = r->sorted[i];

			if (ok_to_out (j, &outqual, p, r)) {
				rank++;

				if (sdev == NULL) {
					sdev_str = "\"-\"";
				} else if (sdev[j] > 0.00000001) {
					sprintf(sdev_str_buffer, "%.1f", sdev[j] * confidence_factor);
					sdev_str = sdev_str_buffer;
				} else {
					sdev_str = "\"-\"";
				}

				fprintf(f, "%d,"
				"\"%s\",%.1f"
				",%s"
				",%.2f"
				",%ld"
				",%.2f"
				"\n"		
				,rank
				,p->name[j]
				,r->ratingof_results[j] 
				,sdev_str
				,r->obtained_results[j]
				,(long)r->playedby_results[j]
				,r->playedby_results[j]==0?0:100.0*r->obtained_results[j]/(double)r->playedby_results[j] 
				);
			}
		}
	}

	return;
}


void
errorsout(const struct PLAYERS *p, const struct RATINGS *r, const struct DEVIATION_ACC *s, const char *out, double confidence_factor)
{
	FILE *f;
	ptrdiff_t idx;
	player_t y,x;
	player_t i;
	player_t j;

	assert (p);
	assert (r);
	assert (s);

	if (NULL != (f = fopen (out, "w"))) {
		fprintf(f, "\"N\",\"NAME\"");	
		for (i = 0; i < p->n; i++) {
			fprintf(f, ",%ld", (long) i);		
		}
		fprintf(f, "\n");	
		for (i = 0; i < p->n; i++) {
			y = r->sorted[i];
			fprintf(f, "%ld,\"%21s\"", (long) i, p->name[y]);
			for (j = 0; j < i; j++) {
				x = r->sorted[j];
				idx = head2head_idx_sdev ((ptrdiff_t)x, (ptrdiff_t)y);
				fprintf(f,",%.1f", s[idx].sdev * confidence_factor);
			}
			fprintf(f, "\n");
		}
		fclose(f);
	} else {
		fprintf(stderr, "Errors with file: %s\n",out);	
	}
	return;
}


void
ctsout(const struct PLAYERS *p, const struct RATINGS *r, const struct DEVIATION_ACC *s, const char *out)
{
	FILE *f;
	ptrdiff_t idx;
	player_t y;
	player_t x;
	player_t i,j;

	assert (p);
	assert (r);
	assert (s);

	if (NULL != (f = fopen (out, "w"))) {

		fprintf(f, "\"N\",\"NAME\"");	
		for (i = 0; i < p->n; i++) {
			fprintf(f, ",%ld",(long) i);		
		}
		fprintf(f, "\n");	

		for (i = 0; i < p->n; i++) {
			y = r->sorted[i];
			fprintf(f, "%ld,\"%21s\"", (long) i, p->name[y]);

			for (j = 0; j < p->n; j++) {
				double ctrs, sd, dr;
				x = r->sorted[j];
				if (x != y) {
					dr = r->ratingof_results[y] - r->ratingof_results[x];
					idx = head2head_idx_sdev ((ptrdiff_t)x, (ptrdiff_t)y);
					sd = s[idx].sdev;
					ctrs = 100*gauss_integral(dr/sd);
					fprintf(f,",%.1f", ctrs);
				} else {
					fprintf(f,",");
				}
			}
			fprintf(f, "\n");
		}
		fclose(f);
	} else {
		fprintf(stderr, "Errors with file: %s\n",out);	
	}
	return;
}


void
look_at_individual_deviation 
			( player_t 			n_players
			, const bool_t *	flagged
			, struct RATINGS *	rat
			, struct ENC *		enc
			, gamesnum_t		n_enc
			, double			white_adv
			, double			beta)
{
	double accum = 0;
	double diff;
	player_t j;
	size_t allocsize = sizeof(double) * (size_t)(n_players+1);
	double *expected = memnew(allocsize);

	if (expected) {
		printf ("\nResidues\n\n");
		calc_expected(enc, n_enc, white_adv, n_players, rat->ratingof, expected, beta);
		for (accum = 0, j = 0; j < n_players; j++) {
			if (!flagged[j]) {
				diff = expected[j] - rat->obtained [j];
				accum += diff * diff / (double)rat->playedby[j];
				printf ("player[%lu] = %+lf\n", (long unsigned) j, diff);
			}
		}		
		free (expected);
	} else {
		fprintf(stderr, "Lack of memory to show individual deviations\n");
	}
	printf ("\nAverage residues = %lf\n", sqrt(accum));
	return;
}


void
look_at_predictions (gamesnum_t n_enc, const struct ENC *enc, const double *ratingof, double beta, double wadv, double dr0)
{
	gamesnum_t e;
	player_t w, b;
	double W,D,L;

	gamesnum_t white_wins = 0, black_wins = 0, total_draw = 0;
	double white_win_exp = 0;
	double white_dra_exp = 0;
	double white_bla_exp = 0;
	
	double pw, pd, pl;

	for (e = 0; e < n_enc; e++) {
		w = enc[e].wh;
		b = enc[e].bl;
		W = (double)enc[e].W;
		D = (double)enc[e].D;
		L = (double)enc[e].L;

		get_pWDL(ratingof[w] + wadv - ratingof[b], &pw, &pd, &pl, dr0, beta);

		white_wins += enc[e].W;
		total_draw += enc[e].D;
		black_wins += enc[e].L;

		white_win_exp += (W + D + L) * pw;
		white_dra_exp += (W + D + L) * pd;
		white_bla_exp += (W + D + L) * pl;
	}

	printf ("------------------------------------\n");
	printf ("Draw Rate = %lf\n", dr0);
	printf ("White Adv = %lf\n", wadv);
	printf ("\n");
	printf ("Observed\n");
	printf ("white wins = %ld\n", (long) white_wins);
	printf ("total draw = %ld\n", (long) total_draw);
	printf ("black wins = %ld\n", (long) black_wins);
	printf ("\n");
	printf ("Predicted\n");
	printf ("white wins = %.2lf\n", white_win_exp);
	printf ("total draw = %.2lf\n", white_dra_exp);
	printf ("black wins = %.2lf\n", white_bla_exp);
	printf ("------------------------------------\n");
	return;
}

