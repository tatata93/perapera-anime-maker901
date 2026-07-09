#include "GLCanvas.h"

GLCanvas::GLCanvas(QWidget* parent) : QOpenGLWidget(parent) {}

void GLCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
}

void GLCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void GLCanvas::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
}
