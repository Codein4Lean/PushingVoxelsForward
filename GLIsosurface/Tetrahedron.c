#include "Tetrahedron.h"

#include <string.h>
#include "Options.h"
#include "TetrahedronTable.h"
#include "Util.h"
#include "MCTable.h"
#include "THierarchy.h"
#include "MemoryPool.h"

void TetrahedronNode_init_top_level(struct TetrahedronNode* out, int branch, int size, vec3 start)
{
	out->child_index = 0;
	out->stored_as_leaf = 0;
	out->hex_init = 0;
	out->gl_init = 0;

	out->prev = 0;
	out->next = 0;
	out->level = 0;
	out->type = 2;
	out->branch = branch;
	out->size = size;
	out->parent = 0;
	out->children[0] = 0;
	out->children[1] = 0;
	//out->refinement_diamond = 0;

	out->vao = 0;
	out->v_vbo = 0;
	out->n_vbo = 0;
	out->ibo = 0;

	out->v_count = 0;
	out->p_count = 0;
	out->snapped_count = 0;
	out->vbo_size = 0;
	out->ibo_size = 0;

	float fsize = (float)size;
	vec3 middle_total;
	vec3_set(middle_total, 0, 0, 0);

	for (int i = 0; i < 4; i++)
	{
		int corner = (USE_REGULAR_MC ? T_CUBE_INDEXES[branch][i] : T_PEM_CUBE_INDEXES[branch][i]);
		if (USE_REGULAR_MC)
			vec3_set(out->vertices[i], MCDX[corner] * fsize + start[0], MCDY[corner] * fsize + start[1], MCDZ[corner] * fsize + start[2]);
		else
			vec3_set(out->vertices[i], PEMMCDX[corner] * fsize + start[0], PEMMCDY[corner] * fsize + start[1], PEMMCDZ[corner] * fsize + start[2]);

		glm_vec_add(middle_total, out->vertices[i], middle_total);
	}

	vec3_set(out->middle, middle_total[0] * 0.25f, middle_total[1] * 0.25f, middle_total[2] * 0.25f);

	float max_length = 0;
	int max_length_index = 0;
	for (size_t i = 0; i < 6; i++)
	{
		float length = vec3_distance2(out->vertices[T_VERTEX_EDGE_MAPS[i][0]], out->vertices[T_VERTEX_EDGE_MAPS[i][1]]);
		if (length > max_length)
		{
			max_length = length;
			max_length_index = i;
		}
	}

	int v0 = T_VERTEX_EDGE_MAPS[max_length_index][0];
	int v1 = T_VERTEX_EDGE_MAPS[max_length_index][1];
	out->refinement_edge[0] = v0;
	out->refinement_edge[1] = v1;
	out->radius = vec3_distance2(out->vertices[v0], out->vertices[v1]);
	vec3_midpoint(out->refinement_key, out->vertices[v0], out->vertices[v1]);
}

void TetrahedronNode_init_child(struct TetrahedronNode* out, struct TetrahedronNode* parent, int child_index, vec3 mv, int* vs)
{
	out->child_index = child_index;
	out->stored_as_leaf = 0;
	out->hex_init = 0;
	out->gl_init = 0;

	out->prev = 0;
	out->next = 0;
	out->level = parent->level + 1;
	out->type = (parent->type + 1) % 3;
	out->branch = parent->branch;
	out->size = parent->size;
	out->parent = parent;
	out->children[0] = 0;
	out->children[1] = 0;
	//out->refinement_diamond = 0;

	out->vao = 0;
	out->v_vbo = 0;
	out->n_vbo = 0;
	out->ibo = 0;

	out->v_count = 0;
	out->p_count = 0;
	out->snapped_count = 0;
	out->vbo_size = 0;
	out->ibo_size = 0;

	vec3 middle_total;
	vec3_set(middle_total, 0, 0, 0);

	for (int i = 0; i < 4; i++)
	{
		int v_index = vs[i];
		if (v_index == -1)
			vec3_copy(mv, out->vertices[i]);
		else
			vec3_copy(parent->vertices[v_index], out->vertices[i]);

		glm_vec_add(middle_total, out->vertices[i], middle_total);
	}
	
	vec3_set(out->middle, middle_total[0] * 0.25f, middle_total[1] * 0.25f, middle_total[2] * 0.25f);

	float max_length = 0;
	int max_length_index = 0;
	for (int i = 0; i < 6; i++)
	{
		float length = vec3_distance2(out->vertices[T_VERTEX_EDGE_MAPS[i][0]], out->vertices[T_VERTEX_EDGE_MAPS[i][1]]);
		if (length > max_length)
		{
			max_length = length;
			max_length_index = i;
		}
	}

	int v0 = T_VERTEX_EDGE_MAPS[max_length_index][0];
	int v1 = T_VERTEX_EDGE_MAPS[max_length_index][1];
	out->refinement_edge[0] = v0;
	out->refinement_edge[1] = v1;
	out->radius = vec3_distance2(out->vertices[v0], out->vertices[v1]);
	vec3_midpoint(out->refinement_key, out->vertices[v0], out->vertices[v1]);
}

void TetrahedronNode_destroy(struct TetrahedronNode* t)
{
	if (t->hex_init)
	{
		for (int i = 0; i < 4; i++)
		{
			Hexahedron_destroy(&t->hexahedra[i]);
		}
	}
	if (t->gl_init)
	{
		glDeleteVertexArrays(1, &t->vao);
		glDeleteBuffers(3, &t->v_vbo);
	}
}

int TetrahedronNode_split(struct TetrahedronNode* t, struct TDiamondStorage* storage)
{
	if (!t->children[0] && !t->children[1])
	{
		int v0[4], v1[4];
		for (int i = 0; i < 4; i++)
		{
			if (i != t->refinement_edge[0])
				v0[i] = i;
			else
				v0[i] = -1;
			if (i != t->refinement_edge[1])
				v1[i] = i;
			else
				v1[i] = -1;
		}

		t->children[0] = poolMalloc(&storage->t_pool);
		t->children[1] = poolMalloc(&storage->t_pool);
		if (!t->children[0] || !t->children[1])
			return 1;

		TetrahedronNode_init_child(t->children[0], t, 0, t->refinement_key, v0);
		TetrahedronNode_init_child(t->children[1], t, 1, t->refinement_key, v1);
		TDiamondStorage_add_tetrahedron(storage, t->children[0]);
		TDiamondStorage_add_tetrahedron(storage, t->children[1]);
	}
}

int TetrahedronNode_add_outline(struct TetrahedronNode* out, vec3** out_verts, uint32_t** out_inds, uint32_t* v_next, uint32_t* v_size, uint32_t* i_next, uint32_t* i_size)
{
	if (*v_next + 4 >= *v_size)
	{
		*v_size *= 2;
		*out_verts = realloc(*out_verts, *v_size * sizeof(vec3));
		if (!*out_verts)
			return 1;
	}

	if (*i_next + 12 >= *i_size)
	{
		*i_size *= 2;
		*out_inds = realloc(*out_inds, *i_size * sizeof(uint32_t));
		if (!*out_verts)
			return 1;
	}

	memcpy((*out_verts)[*v_next], out->vertices, sizeof(vec3) * 4);
	for (int i = 0; i < 12; i++)
	{
		(*out_inds)[*i_next] = *v_next + T_VERTEX_EDGE_MAPS[i / 2][i % 2];
		(*i_next)++;
	}
	(*v_next) += 4;

	if (out->children[0] && TetrahedronNode_add_outline(out->children[0], out_verts, out_inds, v_next, v_size, i_next, i_size))
		return 1;
	if (out->children[1] && TetrahedronNode_add_outline(out->children[1], out_verts, out_inds, v_next, v_size, i_next, i_size))
		return 1;

	return 0;
}

int TetrahedronNode_is_leaf(struct TetrahedronNode* t)
{
	return t->children[0] == 0 && t->children[1] == 0;
}

uint32_t TetrahedronNode_extract(struct TetrahedronNode* t, uint32_t* v_count, uint32_t* p_count, int pem, float threshold, struct osn_context* osn, int sub_resolution)
{
	int return_code = 0;
	vec3* out_vertices = malloc(4096 * sizeof(vec3));
	vec3* out_normals = malloc(4096 * sizeof(vec3));
	uint32_t next_vertex = 0;
	uint32_t out_v_size = 4096;

	uint32_t* out_indexes = malloc(4096 * sizeof(uint32_t));
	uint32_t next_index = 0;
	uint32_t out_i_size = 4096;

	if (!out_vertices || !out_normals || !out_indexes)
	{
		return_code = 1;
		goto Cleanup;
	}

	if (!t->hex_init)
	{
		t->hex_init = 1;
		Hexahedron_init(&t->hexahedra[0], t->vertices, 0, (t->branch & 1) == 0, pem, threshold, sub_resolution);
		Hexahedron_init(&t->hexahedra[1], t->vertices, 1, (t->branch & 1) == 1, pem, threshold, sub_resolution);
		Hexahedron_init(&t->hexahedra[2], t->vertices, 2, (t->branch & 1) == 0, pem, threshold, sub_resolution);
		Hexahedron_init(&t->hexahedra[3], t->vertices, 3, (t->branch & 1) == 1, pem, threshold, sub_resolution);
	}
	else
	{
		t->hexahedra[0].chunk.pem = pem;
		t->hexahedra[0].chunk.snap_threshold = threshold;
		t->hexahedra[1].chunk.pem = pem;
		t->hexahedra[1].chunk.snap_threshold = threshold;
		t->hexahedra[2].chunk.pem = pem;
		t->hexahedra[2].chunk.snap_threshold = threshold;
		t->hexahedra[3].chunk.pem = pem;
		t->hexahedra[3].chunk.snap_threshold = threshold;
	}

	Hexahedron_run(&t->hexahedra[0], &out_vertices, &out_normals, &out_v_size, &next_vertex, &out_indexes, &out_i_size, &next_index, osn);
	Hexahedron_run(&t->hexahedra[1], &out_vertices, &out_normals, &out_v_size, &next_vertex, &out_indexes, &out_i_size, &next_index, osn);
	Hexahedron_run(&t->hexahedra[2], &out_vertices, &out_normals, &out_v_size, &next_vertex, &out_indexes, &out_i_size, &next_index, osn);
	Hexahedron_run(&t->hexahedra[3], &out_vertices, &out_normals, &out_v_size, &next_vertex, &out_indexes, &out_i_size, &next_index, osn);

	t->v_count = t->hexahedra[0].chunk.v_count + t->hexahedra[1].chunk.v_count + t->hexahedra[2].chunk.v_count + t->hexahedra[3].chunk.v_count;
	t->p_count = t->hexahedra[0].chunk.p_count + t->hexahedra[1].chunk.p_count + t->hexahedra[2].chunk.p_count + t->hexahedra[3].chunk.p_count;

	if (!t->v_count)
		goto Cleanup;

	if (!t->gl_init)
	{
		// Vertex buffers
		t->vbo_size = out_v_size;
		glGenBuffers(1, &t->v_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, t->v_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * next_vertex, out_vertices, GL_STATIC_DRAW);

		glGenBuffers(1, &t->n_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, t->n_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * next_vertex, out_normals, GL_STATIC_DRAW);

		// Index buffers
		t->ibo_size = out_i_size;
		glGenBuffers(1, &t->ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * next_index, out_indexes, GL_STATIC_DRAW);
	}
	else
	{
		// Vertex buffers
		if (out_v_size <= t->vbo_size)
		{
			glBindBuffer(GL_ARRAY_BUFFER, t->v_vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * next_vertex, out_vertices, GL_STATIC_DRAW);

			glBindBuffer(GL_ARRAY_BUFFER, t->n_vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * next_vertex, out_normals, GL_STATIC_DRAW);
		}
		else
		{
			glDeleteBuffers(2, &t->v_vbo);
			t->vbo_size = out_v_size;

			glGenBuffers(1, &t->v_vbo);
			glBindBuffer(GL_ARRAY_BUFFER, t->v_vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * next_vertex, out_vertices, GL_STATIC_DRAW);

			glGenBuffers(1, &t->n_vbo);
			glBindBuffer(GL_ARRAY_BUFFER, t->n_vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * next_vertex, out_normals, GL_STATIC_DRAW);
		}

		// Index buffer
		if (out_i_size <= t->ibo_size)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * next_index, out_indexes, GL_STATIC_DRAW);
		}
		else
		{
			glDeleteBuffers(1, &t->ibo);
			t->ibo_size = out_i_size;

			glGenBuffers(1, &t->ibo);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * next_index, out_indexes, GL_STATIC_DRAW);
		}
	}

	if(!t->gl_init)
		glGenVertexArrays(1, &t->vao);

	glBindVertexArray(t->vao);
	glBindBuffer(GL_ARRAY_BUFFER, t->v_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
	glBindBuffer(GL_ARRAY_BUFFER, t->n_vbo);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);

	t->gl_init = 1;

Cleanup:
	free(out_vertices);
	free(out_normals);
	free(out_indexes);

	if (DELETE_AFTER_EXTRACT && t->hex_init)
	{
		t->hex_init = 0;
		for (int i = 0; i < 4; i++)
		{
			Hexahedron_destroy(&t->hexahedra[i]);
		}
	}

	return return_code;
}
