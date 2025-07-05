<?php
include_once 'include/database.php'
?>
<?php
include_once 'include/globals.php'
?>
<?php
include_once 'include/functions.php'
?>

<?php
session_start();
if (!isset($_SESSION['USERNAME'])) {
    header("Location: $DOMAIN");
    die('You are not logged');
}

if (isset($_GET['genres']))
    $genres = intval($_GET['genres']);
if (isset($_GET['series']))
    $series = intval($_GET['series']);
if (isset($_GET['characters']))
    $characters = intval($_GET['characters']);
if (isset($_GET['artists']))
    $artists = intval($_GET['artists']);

$query="";
$headers="";
if (empty($genres) && empty($series) && empty($characters) && empty($artists)) {
    $re = mysqli_query($conn,'select count(0) as total from comics;');
    $row = mysqli_fetch_assoc($re);
    $total_count = $row['total'];
} else {
    $first=0;
    $query = ' where ';
    $headers="&genres=$genres&series=$series&characters=$characters&artists=$artists";
    if (!empty($genres)) {
        $re = mysqli_query($conn,'select genres_name from genres where genres_id= ' . $genres . ';');
        $row = mysqli_fetch_assoc($re);
        $genre_name = $row['genres_name'];
        $query .= 'comics_genres like "%/' . $genres . '/%"';
        $first=1;
    }
    if (!empty($series)) {
        $re = mysqli_query($conn,'select series_name from series where series_id= ' . $series . ';');
        $row = mysqli_fetch_assoc($re);
        $serie_name = $row['series_name'];
        if ($first)
            $query .= ' and ';
        $query .= 'comics_series like "%/' . $series . '/%"';
        $first=1;
    }
    if (!empty($characters)) {
        $re = mysqli_query($conn,'select characters_name from characters where characters_id= ' . $characters . ';');
        $row = mysqli_fetch_assoc($re);
        $character_name = $row['characters_name'];
        if ($first)
            $query .= ' and ';
        $query .= 'comics_characters like "%/' . $characters . '/%"';
        $first=1;
    }
    if (!empty($artists)) {
        $re = mysqli_query($conn,'select artists_name from artists where artists_id= ' . $artists . ';');
        $row = mysqli_fetch_assoc($re);
        $artist_name = $row['artists_name'];
        if ($first)
            $query .= ' and ';
        $query .= 'comics_artists like "%/' . $artists . '/%"';
    }
    $re = mysqli_query($conn,"select count(0) as total from comics" . $query . ';');
    $row = mysqli_fetch_assoc($re);
    $total_count = $row['total'];
}

$page = pget('page',$DEFAULT_PAGE_VALUE);
$results = pget_l('results',$DEFAULT_RESULTS_VALUE,$MAX_RESULTS_VALUE);
$total_pages = ceil($total_count/$results);
if ($page > $total_pages)
    die("Stop It, Get Some Help");
$orderbyvalues = array('id','title','commentcount','views','bookmarkedcount','rating','ratingcount','chapterscount');
$endl = null;
if (isset($_GET['orderby'])) {
    if (in_array($_GET['orderby'],$orderbyvalues)) {
        $direction = 'desc';
        if (isset($_GET['r']) && $_GET['r'] > 0)
            $direction = 'asc';
        $endl = '?orderby=' . $_GET['orderby'] . '&r=' . $_GET['r'];
        $order_sql = 'order by comics_' . $_GET['orderby'] . ' ' . $direction;
    }
}
?>

<!DOCTYPE HTML>
<html>
	<head>
        <title>site</title>
        <meta charset="utf-8"/>
        <?php
        echo "<meta rel='shortcut icon' href='" . $DOMAIN . "/icon.ico' type='image/x-icon' />
            <link type='text/css' rel='stylesheet' href='" . $DOMAIN . "/style.css' />";
        ?>
	</head>
	<body>
        <?php
            if (!empty($query))
                echo "<h1>$genre_name/$serie_name/$character_name/$artist_name</h1>";
            echo "<h1>$total_count/$results~$total_pages/$page</h1>";
            if ($total_pages > 1)
                print_pages($page,$total_pages,'all','?orderby=' . $_GET['orderby'] . "&r=$direction" . $headers);
		?>

        <div class="order-tiles">
            <?php
                echo '<a href="?orderby=title&r=0' . $headers . '"><div class="order-tile">name</div></a>
                <a href="?orderby=id' . $headers . '"><div class="order-tile">id</div></a>
                <a href="?orderby=commentcount' . $headers . '"><div class="order-tile">comments</div></a>
                <a href="?orderby=views' . $headers . '"><div class="order-tile">views</div></a>
                <a href="?orderby=bookmarkedcount' . $headers . '"><div class="order-tile">bookmarked</div></a>
                <a href="?orderby=rating' . $headeres . '"><div class="order-tile">rating</div></a>
                <a href="?orderby=ratingcount' . $headers . '"><div class="order-tile">ratingc</div></a>
                <a href="?orderby=chapterscount' . $headers . '"><div class="order-tile">chaptersc</div></a>'
            ?>
        </div>

        <div class="mtiles">
		<?php
            $re = mysqli_query($conn,"select comics_id,comics_title,comics_thumbnail from comics " . $query . ' ' . $order_sql . " limit " .  ($page-1)*$results . ", " . $results .  ";");
            while ($row = mysqli_fetch_assoc($re)) {
                echo '<a target="_blank" href="' . $DOMAIN . '/c/' . $row['comics_id'] . '">
                    <div class="mtile">
                    <div class="img-container">
                        <img class="mtile-img" src="' . $DOMAIN . '/thumbnails/' . $row['comics_thumbnail'] . '" />
                    </div>'
                    . $row['comics_title'] .
                    "</div>
                    </a>\n";
            }
        ?>
        </div>
        <?php
            if ($total_pages > 1)
                print_pages($page,$total_pages,'all','?orderby=' . $_GET['orderby'] . "&r=$direction" . $headers);
		?>
	</body>
    <? ?>
</html>
