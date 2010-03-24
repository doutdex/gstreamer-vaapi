/*
 *  gstvaapisurface.c - VA surface abstraction
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * SECTION:gstvaapisurface
 * @short_description: VA surface abstraction
 */

#include "config.h"
#include "gstvaapiutils.h"
#include "gstvaapisurface.h"
#include "gstvaapiimage.h"
#include <va/va_backend.h>

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiSurface, gst_vaapi_surface, GST_VAAPI_TYPE_OBJECT);

#define GST_VAAPI_SURFACE_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SURFACE,	\
                                 GstVaapiSurfacePrivate))

struct _GstVaapiSurfacePrivate {
    VASurfaceID         surface_id;
    guint               width;
    guint               height;
    GstVaapiChromaType  chroma_type;
    GPtrArray          *subpictures;
};

enum {
    PROP_0,

    PROP_SURFACE_ID,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_CHROMA_TYPE
};

static void
destroy_subpicture_cb(gpointer subpicture, gpointer user_data)
{
    g_object_unref(subpicture);
}

static void
gst_vaapi_surface_destroy(GstVaapiSurface *surface)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    GstVaapiSurfacePrivate * const priv = surface->priv;
    VAStatus status;

    GST_DEBUG("surface 0x%08x", priv->surface_id);

    if (priv->surface_id != VA_INVALID_SURFACE) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroySurfaces(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            &priv->surface_id, 1
        );
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroySurfaces()"))
            g_warning("failed to destroy surface 0x%08x\n", priv->surface_id);
        priv->surface_id = VA_INVALID_SURFACE;
    }

    if (priv->subpictures) {
        g_ptr_array_foreach(priv->subpictures, destroy_subpicture_cb, NULL);
        g_ptr_array_free(priv->subpictures, TRUE);
        priv->subpictures = NULL;
    }
}

static gboolean
gst_vaapi_surface_create(GstVaapiSurface *surface)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    GstVaapiSurfacePrivate * const priv = surface->priv;
    VASurfaceID surface_id;
    VAStatus status;
    guint format;

    switch (priv->chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV420:
        format = VA_RT_FORMAT_YUV420;
        break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
        format = VA_RT_FORMAT_YUV422;
        break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
        format = VA_RT_FORMAT_YUV444;
        break;
    default:
        GST_DEBUG("unsupported chroma-type %u\n", priv->chroma_type);
        return FALSE;
    }

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateSurfaces(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        priv->width,
        priv->height,
        format,
        1, &surface_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateSurfaces()"))
        return FALSE;

    GST_DEBUG("surface 0x%08x", surface_id);
    priv->surface_id = surface_id;
    return TRUE;
}

static void
gst_vaapi_surface_finalize(GObject *object)
{
    gst_vaapi_surface_destroy(GST_VAAPI_SURFACE(object));

    G_OBJECT_CLASS(gst_vaapi_surface_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSurface        * const surface = GST_VAAPI_SURFACE(object);
    GstVaapiSurfacePrivate * const priv    = surface->priv;

    switch (prop_id) {
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    case PROP_CHROMA_TYPE:
        priv->chroma_type = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSurface * const surface = GST_VAAPI_SURFACE(object);

    switch (prop_id) {
    case PROP_SURFACE_ID:
        g_value_set_uint(value, gst_vaapi_surface_get_id(surface));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_surface_get_width(surface));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_surface_get_height(surface));
        break;
    case PROP_CHROMA_TYPE:
        g_value_set_uint(value, gst_vaapi_surface_get_chroma_type(surface));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_constructed(GObject *object)
{
    GstVaapiSurface * const surface = GST_VAAPI_SURFACE(object);
    GObjectClass *parent_class;

    gst_vaapi_surface_create(surface);

    parent_class = G_OBJECT_CLASS(gst_vaapi_surface_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_surface_class_init(GstVaapiSurfaceClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSurfacePrivate));

    object_class->finalize     = gst_vaapi_surface_finalize;
    object_class->set_property = gst_vaapi_surface_set_property;
    object_class->get_property = gst_vaapi_surface_get_property;
    object_class->constructed  = gst_vaapi_surface_constructed;

    /**
     * GstVaapiSurface:id:
     *
     * The underlying #VASurfaceID of the surface.
     */
    g_object_class_install_property
        (object_class,
         PROP_SURFACE_ID,
         g_param_spec_uint("id",
                           "VA surface id",
                           "The underlying VA surface id",
                           0, G_MAXUINT32, VA_INVALID_SURFACE,
                           G_PARAM_READABLE));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The width of the surface",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Height",
                           "The height of the surface",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_CHROMA_TYPE,
         g_param_spec_uint("chroma-type",
                           "Chroma type",
                           "The chroma type of the surface",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_surface_init(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate *priv = GST_VAAPI_SURFACE_GET_PRIVATE(surface);

    surface->priv       = priv;
    priv->surface_id    = VA_INVALID_SURFACE;
    priv->width         = 0;
    priv->height        = 0;
    priv->chroma_type   = 0;
    priv->subpictures   = NULL;
}

/**
 * gst_vaapi_surface_new:
 * @display: a #GstVaapiDisplay
 * @chroma_type: the surface chroma format
 * @width: the requested surface width
 * @height: the requested surface height
 *
 * Creates a new #GstVaapiSurface with the specified chroma format and
 * dimensions.
 *
 * Return value: the newly allocated #GstVaapiSurface object
 */
GstVaapiSurface *
gst_vaapi_surface_new(
    GstVaapiDisplay    *display,
    GstVaapiChromaType  chroma_type,
    guint               width,
    guint               height
)
{
    GST_DEBUG("size %ux%u, chroma type 0x%x", width, height, chroma_type);

    return g_object_new(GST_VAAPI_TYPE_SURFACE,
                        "display",      display,
                        "width",        width,
                        "height",       height,
                        "chroma-type",  chroma_type,
                        NULL);
}

/**
 * gst_vaapi_surface_get_id:
 * @surface: a #GstVaapiSurface
 *
 * Returns the underlying VASurfaceID of the @surface.
 *
 * Return value: the underlying VA surface id
 */
VASurfaceID
gst_vaapi_surface_get_id(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), VA_INVALID_SURFACE);

    return surface->priv->surface_id;
}

/**
 * gst_vaapi_surface_get_chroma_type:
 * @surface: a #GstVaapiSurface
 *
 * Returns the #GstVaapiChromaType the @surface was created with.
 *
 * Return value: the #GstVaapiChromaType
 */
GstVaapiChromaType
gst_vaapi_surface_get_chroma_type(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->chroma_type;
}

/**
 * gst_vaapi_surface_get_width:
 * @surface: a #GstVaapiSurface
 *
 * Returns the @surface width.
 *
 * Return value: the surface width, in pixels
 */
guint
gst_vaapi_surface_get_width(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->width;
}

/**
 * gst_vaapi_surface_get_height:
 * @surface: a #GstVaapiSurface
 *
 * Returns the @surface height.
 *
 * Return value: the surface height, in pixels.
 */
guint
gst_vaapi_surface_get_height(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->height;
}

/**
 * gst_vaapi_surface_get_size:
 * @surface: a #GstVaapiSurface
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiSurface.
 */
void
gst_vaapi_surface_get_size(
    GstVaapiSurface *surface,
    guint           *pwidth,
    guint           *pheight
)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    if (pwidth)
        *pwidth = gst_vaapi_surface_get_width(surface);

    if (pheight)
        *pheight = gst_vaapi_surface_get_height(surface);
}

/**
 * gst_vaapi_surface_derive_image:
 * @surface: a #GstVaapiSurface
 *
 * Derives a #GstVaapiImage from the @surface. This image buffer can
 * then be mapped/unmapped for direct CPU access. This operation is
 * only possible if the underlying implementation supports direct
 * rendering capabilities and internal surface formats that can be
 * represented with a #GstVaapiImage.
 *
 * When the operation is not possible, the function returns %NULL and
 * the user should then fallback to using gst_vaapi_surface_get_image()
 * or gst_vaapi_surface_put_image() to accomplish the same task in an
 * indirect manner (additional copy).
 *
 * An image created with gst_vaapi_surface_derive_image() should be
 * unreferenced when it's no longer needed. The image and image buffer
 * data structures will be destroyed. However, the surface contents
 * will remain unchanged until destroyed through the last call to
 * g_object_unref().
 *
 * Return value: the newly allocated #GstVaapiImage object, or %NULL
 *   on failure
 */
GstVaapiImage *
gst_vaapi_surface_derive_image(GstVaapiSurface *surface)
{
    GstVaapiDisplay *display;
    VAImage va_image;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    display           = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    va_image.image_id = VA_INVALID_ID;
    va_image.buf      = VA_INVALID_ID;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaDeriveImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface->priv->surface_id,
        &va_image
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaDeriveImage()"))
        return NULL;
    if (va_image.image_id == VA_INVALID_ID || va_image.buf == VA_INVALID_ID)
        return NULL;

    return gst_vaapi_image_new_with_image(display, &va_image);
}

/**
 * gst_vaapi_surface_get_image
 * @surface: a #GstVaapiSurface
 * @image: a #GstVaapiImage
 *
 * Retrieves surface data into a #GstVaapiImage. The @image must have
 * a format supported by the @surface.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_get_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    VAImageID image_id;
    VAStatus status;
    guint width, height;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);

    display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    if (!display)
        return FALSE;

    gst_vaapi_image_get_size(image, &width, &height);
    if (width != surface->priv->width || height != surface->priv->height)
        return FALSE;

    image_id = gst_vaapi_image_get_id(image);
    if (image_id == VA_INVALID_ID)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaGetImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface->priv->surface_id,
        0, 0, width, height,
        image_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaGetImage()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_put_image:
 * @surface: a #GstVaapiSurface
 * @image: a #GstVaapiImage
 *
 * Copies data from a #GstVaapiImage into a @surface. The @image must
 * have a format supported by the @surface.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_put_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    VAImageID image_id;
    VAStatus status;
    guint width, height;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);

    display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    if (!display)
        return FALSE;

    gst_vaapi_image_get_size(image, &width, &height);
    if (width != surface->priv->width || height != surface->priv->height)
        return FALSE;

    image_id = gst_vaapi_image_get_id(image);
    if (image_id == VA_INVALID_ID)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaPutImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface->priv->surface_id,
        image_id,
        0, 0, width, height,
        0, 0, width, height
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaPutImage()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_associate_subpicture:
 * @surface: a #GstVaapiSurface
 * @subpicture: a #GstVaapiSubpicture
 * @src_rect: the sub-rectangle of the source subpicture
 *   image to extract and process. If %NULL, the entire image will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   surface into which the image is rendered. If %NULL, the entire
 *   surface will be used.
 *
 * Associates the @subpicture with the @surface. The @src_rect
 * coordinates and size are relative to the source image bound to
 * @subpicture. The @dst_rect coordinates and size are relative to the
 * target @surface. Note that the @surface holds an additional
 * reference to the @subpicture.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_associate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
)
{
    GstVaapiDisplay *display;
    GstVaapiRectangle src_rect_default, dst_rect_default;
    GstVaapiImage *image;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), FALSE);

    display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    if (!display)
        return FALSE;

    if (!gst_vaapi_surface_deassociate_subpicture(surface, subpicture))
        return FALSE;

    if (!surface->priv->subpictures) {
        surface->priv->subpictures = g_ptr_array_new();
        if (!surface->priv->subpictures)
            return FALSE;
    }

    if (!src_rect) {
        image = gst_vaapi_subpicture_get_image(subpicture);
        if (!image)
            return FALSE;
        src_rect                = &src_rect_default;
        src_rect_default.x      = 0;
        src_rect_default.y      = 0;
        gst_vaapi_image_get_size(
            image,
            &src_rect_default.width,
            &src_rect_default.height
        );
    }

    if (!dst_rect) {
        dst_rect                = &dst_rect_default;
        dst_rect_default.x      = 0;
        dst_rect_default.y      = 0;
        dst_rect_default.width  = surface->priv->width;
        dst_rect_default.height = surface->priv->height;
    }

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaAssociateSubpicture(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        gst_vaapi_subpicture_get_id(subpicture),
        &surface->priv->surface_id, 1,
        src_rect->x, src_rect->y, src_rect->width, src_rect->height,
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height,
        0
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaAssociateSubpicture()"))
        return FALSE;

    g_ptr_array_add(surface->priv->subpictures, g_object_ref(subpicture));
    return TRUE;
}

/**
 * gst_vaapi_surface_deassociate_subpicture:
 * @surface: a #GstVaapiSurface
 * @subpicture: a #GstVaapiSubpicture
 *
 * Deassociates @subpicture from @surface. Other associations are kept.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_deassociate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture
)
{
    GstVaapiDisplay *display;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), FALSE);

    display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    if (!display)
        return FALSE;

    if (!surface->priv->subpictures)
        return TRUE;

    /* First, check subpicture was really associated with this surface */
    if (!g_ptr_array_remove_fast(surface->priv->subpictures, subpicture)) {
        GST_DEBUG("subpicture 0x%08x was not bound to surface 0x%08x",
                  gst_vaapi_subpicture_get_id(subpicture),
                  surface->priv->surface_id);
        return TRUE;
    }

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaDeassociateSubpicture(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        gst_vaapi_subpicture_get_id(subpicture),
        &surface->priv->surface_id, 1
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    g_object_unref(subpicture);
    if (!vaapi_check_status(status, "vaDeassociateSubpicture()"))
        return FALSE;
    return TRUE;
}

/**
 * gst_vaapi_surface_sync:
 * @surface: a #GstVaapiSurface
 *
 * Blocks until all pending operations on the @surface have been
 * completed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_sync(GstVaapiSurface *surface)
{
    GstVaapiDisplay *display;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    display = GST_VAAPI_OBJECT_GET_DISPLAY(surface);
    if (!display)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaSyncSurface(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface->priv->surface_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaSyncSurface()"))
        return FALSE;

    return TRUE;
}
