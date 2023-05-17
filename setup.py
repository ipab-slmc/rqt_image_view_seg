#!/usr/bin/env python

from setuptools import setup
from catkin_pkg.python_setup import generate_distutils_setup

d = generate_distutils_setup(
    packages=['rqt_image_view_seg'],
    package_dir={'': 'src'},
    scripts=['scripts/rqt_image_view_seg'],
    maintainer='William Woodall',
    maintainer_email='william@openrobotics.org',
)

setup(**d)
