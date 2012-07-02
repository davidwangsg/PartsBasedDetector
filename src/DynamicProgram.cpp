/* 
 *  Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  File:    DynamicProgram.cpp
 *  Author:  Hilton Bristow
 *  Created: Jun 21, 2012
 */

#include <limits>
#include "DynamicProgram.hpp"
using namespace cv;
using namespace std;

DynamicProgram::DynamicProgram() {
	// TODO Auto-generated constructor stub

}

DynamicProgram::~DynamicProgram() {
	// TODO Auto-generated destructor stub
}


/*! @brief Reduce a vector of matrices via indexing
 *
 * Reduce a 3D matrix (represented as a vector of matrices using cv::split() )
 * to a 2D matrix by choosing a single element from each ray cast through the
 * third dimension. This loosely emulates Matlab's matrix indexing functionality
 *
 * out[i][j] == in[i][j][ idx[i][j] ], forall i,j
 *
 * @param in the input 3D matrix
 * @param idx the index to choose at each element (idx.size() == in[k].size() forall k)
 * @param out the flatten 2D output matrix
 */
void reducePickIndex(const vector<Mat>& in, const Mat& idx, Mat& out) {

	// error checking
	int K = in.size();
	double minv, maxv;
	minMaxLoc(idx, &minv, &maxv);
	assert(minv >= 0 && maxv < K);
	for (int k = 0; k < K; ++k) assert(in[k].size() == idx.size());

	// allocate the output array
	out.create(in[0].size(), in[0].type());

	// perform the indexing
	int M = in[0].rows;
	int N = in[0].cols;
	vector<const float*> in_ptr(K);
	if (in[0].isContinuous()) { N = M*N; M = 1; }
	for (int m = 0; m < M; ++m) {
		float* out_ptr = out.ptr<float>(m);
		const int*   idx_ptr = idx.ptr<int>(m);
		for (int k = 0; k < K; ++k) in_ptr[k] = in[k].ptr<float>(m);
		for (int n = 0; n < N; ++n) {
			out_ptr[n] = in_ptr[idx_ptr[n]][n];
		}
	}
}


/*! @brief Reduce a vector of matrices via elementwise max
 *
 * Reduce a 3D matrix (represented as a vector of matrices using cv::split() )
 * to a 2D matrix by taking the elementwise maximum across the 3D dimension.
 * Therefore in.size() == out.size() && out.channels() == 1
 *
 * @param in the input 3D matrix
 * @param maxv the output 2D matrix, containing the maximal values
 * @param maxi the output 2D matrix, containing the maximal indices
 */
void reduceMax(const vector<Mat>& in, Mat& maxv, Mat& maxi) {

	// error checking
	int K = in.size();
	assert(K > 1);
	for (int k = 1; k < K; ++k) assert(in[k].size() == in[k-1].size());

	// allocate the output matrices
	maxv.create(in[0].size(), in[0].type());
	maxi.create(in[0].size(), DataType<int>::type);

	int M = in[0].rows;
	int N = in[0].cols;

	vector<const float*> in_ptr(K);
	if (in[0].isContinuous()) { N = M*N; M = 1; }
	for (int m = 0; m < M; ++m) {
		float* maxv_ptr = maxv.ptr<float>(m);
		int* maxi_ptr = maxi.ptr<int>(m);
		for (int k = 0; k < K; ++k) in_ptr[k] = in[k].ptr<float>(m);
		for (int n = 0; n < N; ++n) {
			float v = -numeric_limits<float>::infinity();
			int i = 0;
			for (int k = 0; k < K; ++k) if (in_ptr[k][n] > v) { i = n; v = in_ptr[k][n]; }
			maxi_ptr[n] = i;
			maxv_ptr[n] = v;
		}
	}
}


/*! @brief the square of an integer
 *
 * @param x the input
 * @return x*x
 */
static inline int square(int x) { return x*x; }

/*! @brief Generalized 1D distance transform
 *
 * This method performs the 1D distance transform across the rows of a matrix.
 * It is called twice internally by distanceTransform(), once across the rows
 * and once down the columns (transposed to rows)
 *
 * Only quadratic distance functions are handled
 * y = a.x^2 + b.x
 *
 * @param src pointer to the start of the source data
 * @param dst pointer to the start of the destination data
 * @param ptr pointer to indices
 * @param n the total number of rows
 * @param a the quadratic coefficient
 * @param b the linear coefficient
 */
void DynamicProgram::distanceTransform1D(const float* src, float* dst, int* ptr, int n, float a, float b) {

	int*   v = new int[n];
	float* z = new float[n+1];
	int k = 0;
	v[0] = 0;
	z[0] = +numeric_limits<float>::infinity();
	z[1] = -numeric_limits<float>::infinity();
	for (int q = 1; q < n; ++q) {
	    float s = ((src[q] - src[v[k]]) - b*(q - v[k]) + a*(square(q) - square(v[k]))) / (2*a*(q-v[k]));
	    while (s <= z[k]) {
			// Update pointer
			k--;
			s  = ((src[q] - src[v[k]]) - b*(q - v[k]) + a*(square(q) - square(v[k]))) / (2*a*(q-v[k]));
	    }
	    k++;
	    v[k]   = q;
	    z[k]   = s;
	    z[k+1] = +numeric_limits<float>::infinity();
	}

	k = 0;
	for (int q = 0; q < n; ++q) {
		while (z[k+1] < q) k++;
		dst[q] = a*square(q-v[k]) + b*(q-v[k]) + src[v[k]];
		ptr[q] = v[k];
	}

	delete [] v;
	delete [] z;
}


/*! @brief Generalized distance transform
 *
 * 1-Dimensional generalized distance transform based on the paper:
 * P. Felzenszwalb and D. Huttenlocher, "Distance Transforms of Sampled Functions,"
 * Cornell Technical Report, 2004
 *
 * This is used to reduce the complexity of the dynamic program, namely when all
 * of the cost functions are quadratic. The 2D distance transform is broken down
 * into two 1D transforms since the operation is separable
 *
 * @param score_in the input score
 * @param w the quadratic weights
 * @param score_out the distance transformed score
 * @param Ix the distances in the x direction
 * @param Iy the distances in the y direction
 */
void DynamicProgram::distanceTransform(const Mat& score_in, const vector<float> w, Mat& score_out, Mat& Ix, Mat& Iy) {

	// get the dimensionality of the score
	int M = score_in.rows;
	int N = score_in.cols;

	// get the learned quadratic coefficients
	float ax = w[0];
	float bx = w[1];
	float ay = w[2];
	float by = w[3];

	// allocate the output and working matrices
	score_out.create(Size(M, N), DataType<float>::type);
	Ix.create(Size(N, M), DataType<int>::type);
	Iy.create(Size(M, N), DataType<int>::type);

	Mat score_tmp(Size(N, M), DataType<float>::type);
	Mat row(Size(N, 1), DataType<float>::type);

	// compute the distance transform across the rows
	for (int m = 0; m < M; ++m) {
		distanceTransform1D(score_in.ptr<float>(m), score_tmp.ptr<float>(m), Ix.ptr<int>(m), N, -ax, -bx);
	}

	// transpose the intermediate matrices
	transpose(score_tmp, score_tmp);

	// compute the distance transform down the columns
	for (int n = 0; n < N; ++n) {
		distanceTransform1D(score_tmp.ptr<float>(n), score_out.ptr<float>(n), Iy.ptr<int>(n), M, -ay, -by);

	}

	// transpose back to the original layout
	transpose(score_out, score_out);
	transpose(Iy, Iy);

	// get argmins
	float* row_ptr = row.ptr<float>(0);
	for (int m = 0; m < M; ++m) {
		int* Iy_ptr = Iy.ptr<int>(m);
		int* Ix_ptr = Ix.ptr<int>(m);
		for (int n = 0; n < N; ++n) {
			row_ptr[n] = Iy_ptr[Ix_ptr[n]];
		}
		for (int n = 0; n < N; ++n) {
			Iy_ptr[n] = row_ptr[n];
		}
	}
}


void DynamicProgram::minRecursive(const Part& self, const Part& parent, vector<Mat>& scores, int nparts, int scale) {

	// if this is not a leaf node, request the child messages
	if (!self.isLeaf()) {
		for (int c = 0; c < self.children().size(); ++c) minRecursive(self.children()[c], self, scores, nparts, scale);
	}

	// intermediate results
	vector<Mat> scoresi;
	vector<Mat> Ixi;
	vector<Mat> Iyi;
	// final results
	vector<Mat> Ix;
	vector<Mat> Iy;
	vector<Mat> Ik;

	// calculate the score of each of the mixtures
	int nmixtures = self.nmixtures();
	int Ns = nparts*nmixtures*scale + nmixtures*self.pos();
	int Np = nparts*nmixtures*scale + nmixtures*parent.pos();
	for (int m = 0; m < nmixtures; ++m) {
		// raw score outputs
		Mat score_dt, Ix_dt, Iy_dt;
		Mat score_in = scores[Ns+m];
		// compute the distance transform
		distanceTransform(score_in, self.w()[m], score_dt, Ix_dt, Iy_dt);

		// get the anchor position
		Point anchor = self.anchor();

		// calculate a valid region of interest for the scores
		int X = score_in.cols;
		int Y = score_in.rows;
		int xmin = std::max(anchor.x,   0);
		int ymin = std::max(anchor.y,   0);
		int xmax = std::min(anchor.x+X, X);
		int ymax = std::min(anchor.y+Y, X);
		int xoff = std::max(-anchor.x,  0);
		int yoff = std::max(-anchor.y,  0);

		// shift the score by the Part's offset from its parent
		Mat score = -numeric_limits<float>::infinity() * Mat::ones(score_dt.size(), score_dt.type());
		Mat Ixm   = Mat::zeros(Ix_dt.size(), Ix_dt.type());
		Mat Iym   = Mat::zeros(Iy_dt.size(), Iy_dt.type());
		score(Range(xoff, xoff+xmax-xmin), Range(yoff, yoff+ymax-ymin)) = score_dt(Range(xmin, xmax), Range(ymin, ymax));
		Ixm(Range(xoff, xoff+xmax-xmin), Range(yoff, yoff+ymax-ymin)) = Ix_dt(Range(xmin, xmax), Range(ymin, ymax));
		Iym(Range(xoff, xoff+xmax-xmin), Range(yoff, yoff+ymax-ymin)) = Iy_dt(Range(xmin, xmax), Range(ymin, ymax));

		// push the scores onto the intermediate vectors
		scoresi.push_back(score);
		Ixi.push_back(Ixm);
		Iyi.push_back(Iym);
	}

	// at each parent location, for each parent mixture, compute the best child mixture
	for (int m = 0; m < nmixtures; ++m) {
		vector<float> bias = self.bias()[m];
		vector<Mat> weighted;
		Mat I;
		// weight each of the child scores
		for (int mm = 0; mm < nmixtures; ++mm) {
			weighted.push_back(scores[mm] + bias[mm]);
		}

		// compute the max over the mixtures
		Mat maxv, maxi;
		reduceMax(weighted, maxv, maxi);

		// choose the best indices
		Mat Ixm, Iym;
		reducePickIndex(Ixi, maxi, Ixm);
		reducePickIndex(Iyi, maxi, Iym);
		Ix.push_back(Ixm);
		Iy.push_back(Iym);
		Ik.push_back(maxi);
		scores[Np+m] = maxv;
	}
}




/*! @brief Get the min of a dynamic program
 *
 * Get the min of a dynamic program by starting at the leaf nodes,
 * and passing the scores up to the root node. This is essentially
 * a tail recursive algorithm which we can unfold since we know that
 * the parts are sorted from the root to the leaves
 *
 * The algorithm involves 3 steps:
 * 		(1) Apply distance transform
 * 		(2) Shift by the anchor position of the part wrt the parent
 * 		(3) Downsample if necessary
 *
 * @param parts the parts
 * @param responses the probability densities (pdfs) of part locations (fine to coarse)
 * @param nscales the number of spatial scales in the pyramid
 */
void DynamicProgram::min(Part rootpart, vector<Mat>& responses, int nscales) {

	// error checking
	assert(responses.size() == (rootpart.ndescendants()+1 * rootpart.nmixtures() * nscales));

	// walk from the leaves to the root of the tree, passing message to parent
	int os = 0;
	int scale = 0;
	const int nparts = rootpart.ndescendants()+1;
	const int nmixtures = rootpart.nmixtures();
	const int Y = responses[nparts*nmixtures*scale].rows;
	const int X = responses[nparts*nmixtures*scale].cols;

}

/*! @brief Get the argmin of a dynamic program
 *
 * Get the minimum argument of a dynamic program by traversing down the tree of
 * a dynamic program, returning the locations of the best nodes
 */
vector<Candidate> DynamicProgram::argmin(void) {

	return vector<Candidate>();
}
