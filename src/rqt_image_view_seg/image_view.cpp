/*
 * Copyright (c) 2011, Dirk Thomas, TU Darmstadt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the TU Darmstadt nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <rqt_image_view_seg/image_view.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>

namespace rqt_image_view_seg
{

  ImageView::ImageView()
      : rqt_gui_cpp::Plugin(), widget_(0), num_gridlines_(0), rotate_state_(ROTATE_0), num_clicks_(0), num_clicks_required_(1)
  {
    setObjectName("ImageSegmentationInterface");
  }

  void ImageView::initPlugin(qt_gui_cpp::PluginContext &context)
  {
    widget_ = new QWidget();
    ui_.setupUi(widget_);

    if (context.serialNumber() > 1)
    {
      widget_->setWindowTitle(widget_->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");
    }
    context.addWidget(widget_);

    setColorSchemeList();
    ui_.color_scheme_combo_box->setCurrentIndex(ui_.color_scheme_combo_box->findText("Gray"));

    updateTopicList();
    ui_.topics_combo_box->setCurrentIndex(ui_.topics_combo_box->findText(""));
    connect(ui_.topics_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(onTopicChanged(int)));

    ui_.refresh_topics_push_button->setIcon(QIcon::fromTheme("view-refresh"));
    connect(ui_.refresh_topics_push_button, SIGNAL(pressed()), this, SLOT(updateTopicList()));

    ui_.zoom_1_push_button->setIcon(QIcon::fromTheme("zoom-original"));
    connect(ui_.zoom_1_push_button, SIGNAL(toggled(bool)), this, SLOT(onZoom1(bool)));

    connect(ui_.dynamic_range_check_box, SIGNAL(toggled(bool)), this, SLOT(onDynamicRange(bool)));

    ui_.save_as_image_push_button->setIcon(QIcon::fromTheme("document-save-as"));
    connect(ui_.save_as_image_push_button, SIGNAL(pressed()), this, SLOT(saveImage()));

    connect(ui_.num_gridlines_spin_box, SIGNAL(valueChanged(int)), this, SLOT(updateNumGridlines()));

    reset_clicks_server_ = getNodeHandle().advertiseService("/rqt_image_segmentation/reset_clicks", &ImageView::resetClicks, this);

    // set topic name if passed in as argument
    const QStringList &argv = context.argv();
    if (!argv.empty())
    {
      arg_topic_name = argv[0];
      selectTopic(arg_topic_name);
    }
    pub_topic_custom_ = false;

    ui_.image_frame->setOuterLayout(ui_.image_layout);

    QRegExp rx("([a-zA-Z/][a-zA-Z0-9_/]*)?"); // see http://www.ros.org/wiki/ROS/Concepts#Names.Valid_Names (but also accept an empty field)
    ui_.publish_click_location_topic_line_edit->setValidator(new QRegExpValidator(rx, this));
    connect(ui_.publish_click_location_check_box, SIGNAL(toggled(bool)), this, SLOT(onMousePublish(bool)));
    connect(ui_.image_frame, SIGNAL(mouseLeft(int, int)), this, SLOT(onMouseLeft(int, int)));
    connect(ui_.publish_click_location_topic_line_edit, SIGNAL(editingFinished()), this, SLOT(onPubTopicChanged()));

    connect(ui_.smooth_image_check_box, SIGNAL(toggled(bool)), ui_.image_frame, SLOT(onSmoothImageChanged(bool)));

    connect(ui_.rotate_left_push_button, SIGNAL(clicked(bool)), this, SLOT(onRotateLeft()));
    connect(ui_.rotate_right_push_button, SIGNAL(clicked(bool)), this, SLOT(onRotateRight()));

    // Make sure we have enough space for "XXX °"
    ui_.rotate_label->setMinimumWidth(
        ui_.rotate_label->fontMetrics().width("XXX°"));

    hide_toolbar_action_ = new QAction(tr("Hide toolbar"), this);
    hide_toolbar_action_->setCheckable(true);
    ui_.image_frame->addAction(hide_toolbar_action_);
    connect(hide_toolbar_action_, SIGNAL(toggled(bool)), this, SLOT(onHideToolbarChanged(bool)));
  }

  void ImageView::shutdownPlugin()
  {
    subscriber_.shutdown();
    pub_mouse_left_.shutdown();
    segmented_image_pub_.shutdown();
  }

  void ImageView::saveSettings(qt_gui_cpp::Settings &plugin_settings, qt_gui_cpp::Settings &instance_settings) const
  {
    QString topic = ui_.topics_combo_box->currentText();
    // qDebug("ImageView::saveSettings() topic '%s'", topic.toStdString().c_str());
    instance_settings.setValue("topic", topic);
    instance_settings.setValue("zoom1", ui_.zoom_1_push_button->isChecked());
    instance_settings.setValue("dynamic_range", ui_.dynamic_range_check_box->isChecked());
    instance_settings.setValue("max_range", ui_.max_range_double_spin_box->value());
    instance_settings.setValue("publish_click_location", ui_.publish_click_location_check_box->isChecked());
    instance_settings.setValue("mouse_pub_topic", ui_.publish_click_location_topic_line_edit->text());
    instance_settings.setValue("toolbar_hidden", hide_toolbar_action_->isChecked());
    instance_settings.setValue("num_gridlines", ui_.num_gridlines_spin_box->value());
    instance_settings.setValue("smooth_image", ui_.smooth_image_check_box->isChecked());
    instance_settings.setValue("rotate", rotate_state_);
    instance_settings.setValue("color_scheme", ui_.color_scheme_combo_box->currentIndex());
  }

  bool ImageView::resetClicks(std_srvs::Empty::Request &request, std_srvs::Empty::Response &response)
  {
    num_clicks_ = 0;
    clicked_points_.clear();
    query_labels_.clear();
    return true;
  }

  void ImageView::restoreSettings(const qt_gui_cpp::Settings &plugin_settings, const qt_gui_cpp::Settings &instance_settings)
  {
    bool zoom1_checked = instance_settings.value("zoom1", false).toBool();
    ui_.zoom_1_push_button->setChecked(zoom1_checked);

    bool dynamic_range_checked = instance_settings.value("dynamic_range", false).toBool();
    ui_.dynamic_range_check_box->setChecked(dynamic_range_checked);

    double max_range = instance_settings.value("max_range", ui_.max_range_double_spin_box->value()).toDouble();
    ui_.max_range_double_spin_box->setValue(max_range);

    num_gridlines_ = instance_settings.value("num_gridlines", ui_.num_gridlines_spin_box->value()).toInt();
    ui_.num_gridlines_spin_box->setValue(num_gridlines_);

    QString topic = instance_settings.value("topic", "").toString();
    // don't overwrite topic name passed as command line argument
    if (!arg_topic_name.isEmpty())
    {
      arg_topic_name = "";
    }
    else
    {
      // qDebug("ImageView::restoreSettings() topic '%s'", topic.toStdString().c_str());
      selectTopic(topic);
    }

    bool publish_click_location = instance_settings.value("publish_click_location", true).toBool();
    ui_.publish_click_location_check_box->setChecked(publish_click_location);

    QString pub_topic = instance_settings.value("mouse_pub_topic", "").toString();
    ui_.publish_click_location_topic_line_edit->setText(pub_topic);

    bool toolbar_hidden = instance_settings.value("toolbar_hidden", false).toBool();
    hide_toolbar_action_->setChecked(toolbar_hidden);

    bool smooth_image_checked = instance_settings.value("smooth_image", false).toBool();
    ui_.smooth_image_check_box->setChecked(smooth_image_checked);

    rotate_state_ = static_cast<RotateState>(instance_settings.value("rotate", 0).toInt());
    if (rotate_state_ >= ROTATE_STATE_COUNT)
      rotate_state_ = ROTATE_0;
    syncRotateLabel();

    int color_scheme = instance_settings.value("color_scheme", ui_.color_scheme_combo_box->currentIndex()).toInt();
    ui_.color_scheme_combo_box->setCurrentIndex(color_scheme);
  }

  void ImageView::setColorSchemeList()
  {
    static const std::map<std::string, int> COLOR_SCHEME_MAP{
        {"Gray", -1}, // Special case: no color map
        {"Autumn", cv::COLORMAP_AUTUMN},
        {"Bone", cv::COLORMAP_BONE},
        {"Cool", cv::COLORMAP_COOL},
        {"Hot", cv::COLORMAP_HOT},
        {"Hsv", cv::COLORMAP_HSV},
        {"Jet", cv::COLORMAP_JET},
        {"Ocean", cv::COLORMAP_OCEAN},
        {"Pink", cv::COLORMAP_PINK},
        {"Rainbow", cv::COLORMAP_RAINBOW},
        {"Spring", cv::COLORMAP_SPRING},
        {"Summer", cv::COLORMAP_SUMMER},
        {"Winter", cv::COLORMAP_WINTER}};

    for (const auto &kv : COLOR_SCHEME_MAP)
    {
      ui_.color_scheme_combo_box->addItem(QString::fromStdString(kv.first), QVariant(kv.second));
    }
  }

  void ImageView::updateTopicList()
  {
    QSet<QString> message_types;
    message_types.insert("sensor_msgs/Image");
    QSet<QString> message_sub_types;
    message_sub_types.insert("sensor_msgs/CompressedImage");

    // get declared transports
    QList<QString> transports;
    image_transport::ImageTransport it(getNodeHandle());
    std::vector<std::string> declared = it.getDeclaredTransports();
    for (std::vector<std::string>::const_iterator it = declared.begin(); it != declared.end(); it++)
    {
      // qDebug("ImageView::updateTopicList() declared transport '%s'", it->c_str());
      QString transport = it->c_str();

      // strip prefix from transport name
      QString prefix = "image_transport/";
      if (transport.startsWith(prefix))
      {
        transport = transport.mid(prefix.length());
      }
      transports.append(transport);
    }

    QString selected = ui_.topics_combo_box->currentText();

    // fill combo box
    QList<QString> topics = getTopics(message_types, message_sub_types, transports).values();
    topics.append("");
    qSort(topics);
    ui_.topics_combo_box->clear();
    for (QList<QString>::const_iterator it = topics.begin(); it != topics.end(); it++)
    {
      QString label(*it);
      label.replace(" ", "/");
      ui_.topics_combo_box->addItem(label, QVariant(*it));
    }

    // restore previous selection
    selectTopic(selected);
  }

  QList<QString> ImageView::getTopicList(const QSet<QString> &message_types, const QList<QString> &transports)
  {
    QSet<QString> message_sub_types;
    return getTopics(message_types, message_sub_types, transports).values();
  }

  QSet<QString> ImageView::getTopics(const QSet<QString> &message_types, const QSet<QString> &message_sub_types, const QList<QString> &transports)
  {
    ros::master::V_TopicInfo topic_info;
    ros::master::getTopics(topic_info);

    QSet<QString> all_topics;
    for (ros::master::V_TopicInfo::const_iterator it = topic_info.begin(); it != topic_info.end(); it++)
    {
      all_topics.insert(it->name.c_str());
    }

    QSet<QString> topics;
    for (ros::master::V_TopicInfo::const_iterator it = topic_info.begin(); it != topic_info.end(); it++)
    {
      if (message_types.contains(it->datatype.c_str()))
      {
        QString topic = it->name.c_str();

        // add raw topic
        topics.insert(topic);
        // qDebug("ImageView::getTopics() raw topic '%s'", topic.toStdString().c_str());

        // add transport specific sub-topics
        for (QList<QString>::const_iterator jt = transports.begin(); jt != transports.end(); jt++)
        {
          if (all_topics.contains(topic + "/" + *jt))
          {
            QString sub = topic + " " + *jt;
            topics.insert(sub);
            // qDebug("ImageView::getTopics() transport specific sub-topic '%s'", sub.toStdString().c_str());
          }
        }
      }
      if (message_sub_types.contains(it->datatype.c_str()))
      {
        QString topic = it->name.c_str();
        int index = topic.lastIndexOf("/");
        if (index != -1)
        {
          topic.replace(index, 1, " ");
          topics.insert(topic);
          // qDebug("ImageView::getTopics() transport specific sub-topic '%s'", topic.toStdString().c_str());
        }
      }
    }
    return topics;
  }

  void ImageView::selectTopic(const QString &topic)
  {
    int index = ui_.topics_combo_box->findText(topic);
    if (index == -1)
    {
      // add topic name to list if not yet in
      QString label(topic);
      label.replace(" ", "/");
      ui_.topics_combo_box->addItem(label, QVariant(topic));
      index = ui_.topics_combo_box->findText(topic);
    }
    ui_.topics_combo_box->setCurrentIndex(index);
  }

  void ImageView::onTopicChanged(int index)
  {
    conversion_mat_.release();

    subscriber_.shutdown();

    // reset image on topic change
    ui_.image_frame->setImage(QImage());

    QStringList parts = ui_.topics_combo_box->itemData(index).toString().split(" ");
    QString topic = parts.first();
    QString transport = parts.length() == 2 ? parts.last() : "raw";

    if (!topic.isEmpty())
    {
      image_transport::ImageTransport it(getNodeHandle());
      image_transport::TransportHints hints(transport.toStdString());
      try
      {
        subscriber_ = it.subscribe(topic.toStdString(), 1, &ImageView::callbackImage, this, hints);
        // qDebug("ImageView::onTopicChanged() to topic '%s' with transport '%s'", topic.toStdString().c_str(), subscriber_.getTransport().c_str());
      }
      catch (image_transport::TransportLoadException &e)
      {
        QMessageBox::warning(widget_, tr("Loading image transport plugin failed"), e.what());
      }
    }

    onMousePublish(ui_.publish_click_location_check_box->isChecked());
  }

  void ImageView::onZoom1(bool checked)
  {
    if (checked)
    {
      if (ui_.image_frame->getImage().isNull())
      {
        return;
      }
      ui_.image_frame->setInnerFrameFixedSize(ui_.image_frame->getImage().size());
    }
    else
    {
      ui_.image_frame->setInnerFrameMinimumSize(QSize(80, 60));
      ui_.image_frame->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
      widget_->setMinimumSize(QSize(80, 60));
      widget_->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    }
  }

  void ImageView::onDynamicRange(bool checked)
  {
    ui_.max_range_double_spin_box->setEnabled(!checked);
  }

  void ImageView::updateNumGridlines()
  {
    num_gridlines_ = ui_.num_gridlines_spin_box->value();
  }

  void ImageView::saveImage()
  {
    // take a snapshot before asking for the filename
    QImage img = ui_.image_frame->getImageCopy();

    QString file_name = QFileDialog::getSaveFileName(widget_, tr("Save as image"), "image.png", tr("Image (*.bmp *.jpg *.png *.tiff)"));
    if (file_name.isEmpty())
    {
      return;
    }

    img.save(file_name);
  }

  void ImageView::onMousePublish(bool checked)
  {
    std::string topicName;
    if (pub_topic_custom_)
    {
      topicName = ui_.publish_click_location_topic_line_edit->text().toStdString();
    }
    else
    {
      if (!subscriber_.getTopic().empty())
      {
        topicName = subscriber_.getTopic() + "_mouse_left";
      }
      else
      {
        topicName = "mouse_left";
      }
      ui_.publish_click_location_topic_line_edit->setText(QString::fromStdString(topicName));
    }

    if (checked)
    {
      pub_mouse_left_ = getNodeHandle().advertise<geometry_msgs::Point>("/rqt_image_segmentation/click_point", 1000);
      segmented_image_pub_ = getNodeHandle().advertise<sensor_msgs::Image>("/rqt_image_segmentation/masked_image", 1000);
      mask_pub_ = getNodeHandle().advertise<sensor_msgs::Image>("/rqt_image_segmentation/mask", 1000);
      segmentation_client_ = getNodeHandle().serviceClient<ros_sam::Segmentation>("/sam_node/segment");
    }
    else
    {
      pub_mouse_left_.shutdown();
      segmented_image_pub_.shutdown();
      mask_pub_.shutdown();
    }
  }

  void ImageView::onMouseLeft(int x, int y)
  {
    if (ui_.publish_click_location_check_box->isChecked() && !ui_.image_frame->getImage().isNull())
    {
      num_clicks_++;
      geometry_msgs::PointStamped clickCanvasLocation;
      // Publish click location in pixel coordinates
      ros::Time click_time;
      clickCanvasLocation.header.stamp = click_time.fromNSec(image_time_);
      clickCanvasLocation.point.x = round((double)x / (double)ui_.image_frame->width() * (double)ui_.image_frame->getImage().width());
      clickCanvasLocation.point.y = round((double)y / (double)ui_.image_frame->height() * (double)ui_.image_frame->getImage().height());
      clickCanvasLocation.point.z = 0;

      geometry_msgs::Point clickLocation = clickCanvasLocation.point;

      switch (rotate_state_)
      {
      case ROTATE_90:
        clickLocation.x = clickCanvasLocation.point.y;
        clickLocation.y = ui_.image_frame->getImage().width() - clickCanvasLocation.point.x;
        break;
      case ROTATE_180:
        clickLocation.x = ui_.image_frame->getImage().width() - clickCanvasLocation.point.x;
        clickLocation.y = ui_.image_frame->getImage().height() - clickCanvasLocation.point.y;
        break;
      case ROTATE_270:
        clickLocation.x = ui_.image_frame->getImage().height() - clickCanvasLocation.point.y;
        clickLocation.y = clickCanvasLocation.point.x;
        break;
      default:
        break;
      }

      clicked_points_.push_back(clickLocation);
      // if(clicked_points_.size() %2 == 0){
      ROS_INFO("Include point");
      query_labels_.push_back(1);
      // } else {
      //   ROS_INFO("Exclude point");
      //   query_labels_.push_back(0);
      // }
      
      ROS_INFO_STREAM("Got " << num_clicks_ << " clicks " << clickLocation.x << " " << clickLocation.y);

      if (num_clicks_ >= num_clicks_required_)
      {
        num_clicks_ = 0;

        ROS_INFO_STREAM("Requesting Segmentation on " << clicked_points_.size() << " points ");

        pub_mouse_left_.publish(clickLocation);

        ros_sam::Segmentation srv;
        srv.request.image = last_img_msg_;
        srv.request.query_points = clicked_points_;

        srv.request.query_labels = query_labels_;
        srv.request.multimask = true;
        srv.request.logits = false;


        if (segmentation_client_.waitForExistence(ros::Duration(1)))
        {
          ROS_INFO("Found service");
        }
        else
        {
          ROS_ERROR("Can't Find Service");
          return;
        }

        if (segmentation_client_.call(srv))
        {
          // float max = 0.0;
          // int max_idx = 0;
          // for (int i = 0; i < srv.response.scores.size(); i++){
          //   if(srv.response.scores[i] > max){
          //     max = srv.response.scores[i];
          //     max_idx = i;
          //   }
          // }
          // ROS_INFO_STREAM("Highest index: " << max_idx);
          sensor_msgs::ImagePtr mask_msg = createMaskMsg(srv.response.masks[0]);
          sensor_msgs::ImagePtr masked_image_msg = createMaskedImageMsg(last_img_msg_, mask_msg, 2u);
          mask_pub_.publish(mask_msg);
          segmented_image_pub_.publish(*masked_image_msg);
        }
        else
        {
          ROS_ERROR("Failed to call service");
        }

        clicked_points_.clear();
        query_labels_.clear();
      }
    }
  }

  sensor_msgs::ImagePtr ImageView::createMaskMsg(sensor_msgs::Image mask_msg)
  {

    cv_bridge::CvImagePtr mask_ptr, masked_image_ptr;
    try
    {
      mask_ptr = cv_bridge::toCvCopy(mask_msg, sensor_msgs::image_encodings::TYPE_8UC1);
    }
    catch (cv_bridge::Exception &e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return masked_image_ptr->toImageMsg();
    }

    mask_ptr->image *= 255;

    return mask_ptr->toImageMsg();
  }
 
  // Applies a color mask to an image
  sensor_msgs::ImagePtr ImageView::createMaskedImageMsg(sensor_msgs::Image& image_msg, sensor_msgs::ImagePtr mask_msg, uint8_t color)
  {
    if (color > 2) {
      ROS_ERROR("color must be in {0, 1, 2} to indicate a mask of red, green or blue");
    }

    cv_bridge::CvImagePtr image_ptr, mask_ptr, masked_image_ptr;
    try
    {
      image_ptr = cv_bridge::toCvCopy(image_msg, sensor_msgs::image_encodings::RGB8);
      mask_ptr = cv_bridge::toCvCopy(*mask_msg, sensor_msgs::image_encodings::TYPE_8UC1);
    }
    catch (cv_bridge::Exception &e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return masked_image_ptr->toImageMsg();
    }

    masked_image_ptr = image_ptr;

    cv::Mat out_image;
    std::vector<cv::Mat> rgbChannels;
    cv::Mat zeros = cv::Mat::zeros(cv::Size(masked_image_ptr->image.cols, masked_image_ptr->image.rows), CV_8UC1);
    cv::Mat mask = mask_ptr->image;
    for (int i = 0; i < 3; i++)
    {

      if (i == color)
      {
        rgbChannels.push_back(mask);
      }
      else
      {
        rgbChannels.push_back(zeros);
      }
    }

    cv::merge(rgbChannels, out_image);

    masked_image_ptr->image += out_image;
    return masked_image_ptr->toImageMsg();
  }

  void ImageView::onPubTopicChanged()
  {
    pub_topic_custom_ = !(ui_.publish_click_location_topic_line_edit->text().isEmpty());
    onMousePublish(ui_.publish_click_location_check_box->isChecked());
  }

  void ImageView::onHideToolbarChanged(bool hide)
  {
    ui_.toolbar_widget->setVisible(!hide);
  }

  void ImageView::onRotateLeft()
  {
    int m = rotate_state_ - 1;
    if (m < 0)
      m = ROTATE_STATE_COUNT - 1;

    rotate_state_ = static_cast<RotateState>(m);
    syncRotateLabel();
  }

  void ImageView::onRotateRight()
  {
    rotate_state_ = static_cast<RotateState>((rotate_state_ + 1) % ROTATE_STATE_COUNT);
    syncRotateLabel();
  }

  void ImageView::syncRotateLabel()
  {
    switch (rotate_state_)
    {
    default:
    case ROTATE_0:
      ui_.rotate_label->setText("0°");
      break;
    case ROTATE_90:
      ui_.rotate_label->setText("90°");
      break;
    case ROTATE_180:
      ui_.rotate_label->setText("180°");
      break;
    case ROTATE_270:
      ui_.rotate_label->setText("270°");
      break;
    }
  }

  void ImageView::invertPixels(int x, int y)
  {
    // Could do 255-conversion_mat_.at<cv::Vec3b>(cv::Point(x,y))[i], but that doesn't work well on gray
    cv::Vec3b &pixel = conversion_mat_.at<cv::Vec3b>(cv::Point(x, y));
    if (pixel[0] + pixel[1] + pixel[2] > 3 * 127)
      pixel = cv::Vec3b(0, 0, 0);
    else
      pixel = cv::Vec3b(255, 255, 255);
  }

  QList<int> ImageView::getGridIndices(int size) const
  {
    QList<int> indices;

    // the spacing between adjacent grid lines
    float grid_width = 1.0f * size / (num_gridlines_ + 1);

    // select grid line(s) closest to the center
    float index;
    if (num_gridlines_ % 2) // odd
    {
      indices.append(size / 2);
      // make the center line 2px wide in case of an even resolution
      if (size % 2 == 0) // even
        indices.append(size / 2 - 1);
      index = 1.0f * (size - 1) / 2;
    }
    else // even
    {
      index = grid_width * (num_gridlines_ / 2);
      // one grid line before the center
      indices.append(round(index));
      // one grid line after the center
      indices.append(size - 1 - round(index));
    }

    // add additional grid lines from the center to the border of the image
    int lines = (num_gridlines_ - 1) / 2;
    while (lines > 0)
    {
      index -= grid_width;
      indices.append(round(index));
      indices.append(size - 1 - round(index));
      lines--;
    }

    return indices;
  }

  void ImageView::overlayGrid()
  {
    // vertical gridlines
    QList<int> columns = getGridIndices(conversion_mat_.cols);
    for (QList<int>::const_iterator x = columns.begin(); x != columns.end(); ++x)
    {
      for (int y = 0; y < conversion_mat_.rows; ++y)
      {
        invertPixels(*x, y);
      }
    }

    // horizontal gridlines
    QList<int> rows = getGridIndices(conversion_mat_.rows);
    for (QList<int>::const_iterator y = rows.begin(); y != rows.end(); ++y)
    {
      for (int x = 0; x < conversion_mat_.cols; ++x)
      {
        invertPixels(x, *y);
      }
    }
  }

  void ImageView::callbackImage(const sensor_msgs::Image::ConstPtr &msg)
  {
    try
    {
      // First let cv_bridge do its magic
      cv_bridge::CvImageConstPtr cv_ptr = cv_bridge::toCvShare(msg, sensor_msgs::image_encodings::RGB8);
      conversion_mat_ = cv_ptr->image;

      image_time_ = msg->header.stamp.toNSec();
      last_img_msg_ = *msg;

      if (num_gridlines_ > 0)
        overlayGrid();
    }
    catch (cv_bridge::Exception &e)
    {
      try
      {
        // If we're here, there is no conversion that makes sense, but let's try to imagine a few first
        cv_bridge::CvImageConstPtr cv_ptr = cv_bridge::toCvShare(msg);
        if (msg->encoding == "CV_8UC3")
        {
          // assuming it is rgb
          conversion_mat_ = cv_ptr->image;
        }
        else if (msg->encoding == "8UC1")
        {
          // convert gray to rgb
          cv::cvtColor(cv_ptr->image, conversion_mat_, CV_GRAY2RGB);
        }
        else if (msg->encoding == "16UC1" || msg->encoding == "32FC1")
        {
          // scale / quantify
          double min = 0;
          double max = ui_.max_range_double_spin_box->value();
          if (msg->encoding == "16UC1")
            max *= 1000;
          if (ui_.dynamic_range_check_box->isChecked())
          {
            // dynamically adjust range based on min/max in image
            cv::minMaxLoc(cv_ptr->image, &min, &max);
            if (min == max)
            {
              // completely homogeneous images are displayed in gray
              min = 0;
              max = 2;
            }
          }
          cv::Mat img_scaled_8u;
          cv::Mat(cv_ptr->image - min).convertTo(img_scaled_8u, CV_8UC1, 255. / (max - min));

          const auto color_scheme_index = ui_.color_scheme_combo_box->currentIndex();
          const auto color_scheme = ui_.color_scheme_combo_box->itemData(color_scheme_index).toInt();
          if (color_scheme == -1)
          {
            cv::cvtColor(img_scaled_8u, conversion_mat_, CV_GRAY2RGB);
          }
          else
          {
            cv::Mat img_color_scheme;
            cv::applyColorMap(img_scaled_8u, img_color_scheme, color_scheme);
            cv::cvtColor(img_color_scheme, conversion_mat_, CV_BGR2RGB);
          }
        }
        else
        {
          qWarning("ImageView.callback_image() could not convert image from '%s' to 'rgb8' (%s)", msg->encoding.c_str(), e.what());
          ui_.image_frame->setImage(QImage());
          return;
        }
      }
      catch (cv_bridge::Exception &e)
      {
        qWarning("ImageView.callback_image() while trying to convert image from '%s' to 'rgb8' an exception was thrown (%s)", msg->encoding.c_str(), e.what());
        ui_.image_frame->setImage(QImage());
        return;
      }
    }

    // Handle rotation
    switch (rotate_state_)
    {
    case ROTATE_90:
    {
      cv::Mat tmp;
      cv::transpose(conversion_mat_, tmp);
      cv::flip(tmp, conversion_mat_, 1);
      break;
    }
    case ROTATE_180:
    {
      cv::Mat tmp;
      cv::flip(conversion_mat_, tmp, -1);
      conversion_mat_ = tmp;
      break;
    }
    case ROTATE_270:
    {
      cv::Mat tmp;
      cv::transpose(conversion_mat_, tmp);
      cv::flip(tmp, conversion_mat_, 0);
      break;
    }
    default:
      break;
    }

    // image must be copied since it uses the conversion_mat_ for storage which is asynchronously overwritten in the next callback invocation
    QImage image(conversion_mat_.data, conversion_mat_.cols, conversion_mat_.rows, conversion_mat_.step[0], QImage::Format_RGB888);
    ui_.image_frame->setImage(image);

    if (!ui_.zoom_1_push_button->isEnabled())
    {
      ui_.zoom_1_push_button->setEnabled(true);
    }
    // Need to update the zoom 1 every new image in case the image aspect ratio changed,
    // though could check and see if the aspect ratio changed or not.
    onZoom1(ui_.zoom_1_push_button->isChecked());
  }
}

PLUGINLIB_EXPORT_CLASS(rqt_image_view_seg::ImageView, rqt_gui_cpp::Plugin)
