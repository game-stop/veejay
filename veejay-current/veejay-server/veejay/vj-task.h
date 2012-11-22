/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2012 Niels Elburg <elburg@hio.hen.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#ifndef VJTASK_H
#define VJTASK_H
typedef	void	*(*performer_job_routine)(void *);

typedef struct
{
	uint8_t *input[4];
	uint8_t *output[4];
	int	strides[4];
	uint8_t *temp[4];
	float fparam;
	int   iparam;
	void	*priv;
	int   width;
	int   height;
	int   shiftv;
	int   shifth;
} vj_task_arg_t;

void	vj_task_init();

int	vj_task_run(uint8_t **buf1, uint8_t **buf2, uint8_t **buf3, int *strides,int n_planes, performer_job_routine func );


void	vj_task_alloc_internal_buf( int w );
void	vj_task_set_float( float f );
void	vj_task_set_int( int i );
void	vj_task_set_wid( int w );
void	vj_task_set_hei( int h );
void	vj_task_set_shift( int h, int v );


int	task_start(int max_workers);
void	task_stop(int max_workers);
void	task_init();
int	task_num_cpus();

void	performer_job( int job_num );

int	num_threaded_tasks();

#endif

