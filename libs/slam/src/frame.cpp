#include <openslam/slam/frame.h>
#include <openslam/slam/feature.h>

namespace openslam
{
	namespace slam
	{
		long unsigned int Frame::frame_counter_ = 0;
		bool Frame::is_initial_computations_ = true;
		float Frame::min_bound_x_, Frame::min_bound_y_, Frame::max_bound_x_, Frame::max_bound_y_;
		float Frame::grid_element_height_, Frame::grid_element_width_;

		Frame::Frame(PinholeCamera* cam, const cv::Mat& img, double timestamp, ORBextractor* extractor) :
			id_(frame_counter_++),
			cam_(cam),
			img_(img),
			timestamp_(timestamp),
			extractor_(extractor)
		{
			// ��ȡ�߶���ز���
			levels_num_ = extractor_->getLevels();
			scale_factor_ = extractor_->getScaleFactor();
			// ֡��ʼ���Ľ���������⣬��ʼ��������������
			extractORB(img);

			// ��Ҫ���ڵ�һ�μ���
			if (is_initial_computations_)
			{
				computeImageBounds(img);

				grid_element_height_ = (max_bound_x_ - min_bound_x_) / static_cast<float>(FRAME_GRID_COLS);
				grid_element_width_ = (max_bound_y_ - min_bound_y_) / static_cast<float>(FRAME_GRID_ROWS);

				is_initial_computations_ = false;
			}
			assignFeaturesToGrid();
		}

		Frame::~Frame()
		{
			std::for_each(features_.begin(), features_.end(), [&](Feature* i){delete i; });
		}

		void Frame::extractORB(const cv::Mat &image)
		{
			std::vector<cv::KeyPoint> vec_keypoints;
			cv::Mat descriptors;
			(*extractor_)(image, cv::Mat(), vec_keypoints, descriptors);
			keypoints_num_ = vec_keypoints.size();
			if (keypoints_num_ == 0) return;

			features_.reserve(keypoints_num_);
			for (size_t i = 0; i < vec_keypoints.size();i++)
			{
				Feature *fea = new Feature(this, vec_keypoints[i], descriptors.row(i));
				features_.push_back(fea);
			}
		}

		void Frame::computeImageBounds(const cv::Mat &image)
		{
			// ����ǻ���ͼ��
			if (cam_->distCoef().at<float>(0) != 0.0)
			{
				cv::Mat mat = (cv::Mat_<float>(4, 2) << 0.0f, 0.0f,
					image.cols, 0.0f,
					0.0f, image.rows,
					image.cols, image.rows);

				// ͨ��mat���ͣ�ת��4����Լ�ͼ���4���߽ǵ㣬���л������
				mat = mat.reshape(2);
				cv::undistortPoints(mat, mat, cam_->cvK(), cam_->distCoef(), cv::Mat(), cam_->cvK());
				mat = mat.reshape(1);
				// �Խ���֮��ĵ�ѡ�������С�߽�ֵ��Ҳ�������������½��бȽϻ�ȡx����Сֵ
				min_bound_x_ = std::min(mat.at<float>(0, 0), mat.at<float>(2, 0));
				max_bound_x_ = std::max(mat.at<float>(1, 0), mat.at<float>(3, 0));
				min_bound_y_ = std::min(mat.at<float>(0, 1), mat.at<float>(1, 1));
				max_bound_y_ = std::max(mat.at<float>(2, 1), mat.at<float>(3, 1));

			}
			else
			{
				min_bound_x_ = 0.0f;
				max_bound_x_ = image.cols;
				min_bound_y_ = 0.0f;
				max_bound_y_ = image.rows;
			}
		}

		bool Frame::posInGrid(const cv::KeyPoint &kp, int &pos_x, int &pos_y)
		{
			pos_x = round((kp.pt.x - min_bound_x_)*(1 / grid_element_height_));
			pos_y = round((kp.pt.y - min_bound_y_)*(1 / grid_element_width_));

			//����������������ǽ����˻�������ģ������п�����ͼ����ߣ����Ҫ�����ж�
			if (pos_x < 0 || pos_x >= FRAME_GRID_COLS || pos_y < 0 || pos_y >= FRAME_GRID_ROWS)
				return false;

			return true;
		}

		void Frame::assignFeaturesToGrid()
		{
			// ���������䵽������
			int reserve_num = 0.5f*keypoints_num_ / (FRAME_GRID_COLS*FRAME_GRID_ROWS);
			for (unsigned int i = 0; i < FRAME_GRID_COLS; i++)
				for (unsigned int j = 0; j < FRAME_GRID_ROWS; j++)
					grid_[i][j].reserve(reserve_num);

			for (int i = 0; i < keypoints_num_; i++)
			{
				const cv::KeyPoint &kp = features_[i]->undistored_keypoint_;

				int grid_pos_x, grid_pos_y;
				if (posInGrid(kp, grid_pos_x, grid_pos_y))
					grid_[grid_pos_x][grid_pos_y].push_back(i);
			}
		}

		std::vector<size_t> Frame::getFeaturesInArea(const float &x, const float  &y, 
			const float  &r, const int min_level, const int max_level) const
		{
			std::vector<size_t> indices;
			indices.reserve(keypoints_num_);
			// �����������Ƶ����ӵķ�Χ����
			const int min_cell_x = std::max(0, (int)floor((x - min_bound_x_ - r)*(1 / grid_element_height_)));
			if (min_cell_x >= FRAME_GRID_COLS)
				return indices;

			const int max_cell_x = std::min((int)FRAME_GRID_COLS - 1, (int)ceil((x - min_bound_x_ + r)*(1 / grid_element_height_)));
			if (max_cell_x < 0)
				return indices;

			const int min_cell_y = std::max(0, (int)floor((y - min_bound_y_ - r)*(1 / grid_element_width_)));
			if (min_cell_y >= FRAME_GRID_ROWS)
				return indices;

			const int max_cell_y = std::min((int)FRAME_GRID_ROWS - 1, (int)ceil((y - min_bound_y_ + r)*(1 / grid_element_width_)));
			if (max_cell_y < 0)
				return indices;

			const bool check_levels = (min_level>0) || (max_level >= 0);

			for (int ix = min_cell_x; ix <= max_cell_x; ix++)
			{
				for (int iy = min_cell_y; iy <= max_cell_y; iy++)
				{
					// �洢�������������
					const std::vector<size_t> cell = grid_[ix][iy];
					if (cell.empty())
						continue;

					for (size_t j = 0, jend = cell.size(); j < jend; j++)
					{
						const cv::KeyPoint &undistored_keypoint = features_[cell[j]]->undistored_keypoint_;
						// �ٴζԳ߶Ƚ�һ�����
						if (check_levels)
						{
							if (undistored_keypoint.octave<min_level)
								continue;
							if (max_level >= 0)
							{
								if (undistored_keypoint.octave > max_level)
									continue;
							}
						}
						// �ٴ�ȷ���������Ƿ��ڰ뾶r�ķ�Χ��Ѱ�ҵ�
						const float distx = undistored_keypoint.pt.x - x;
						const float disty = undistored_keypoint.pt.y - y;
						if (fabs(distx) < r && fabs(disty) < r)
							indices.push_back(cell[j]);
					}
				}
			}

			return indices;
		}

	}
}
