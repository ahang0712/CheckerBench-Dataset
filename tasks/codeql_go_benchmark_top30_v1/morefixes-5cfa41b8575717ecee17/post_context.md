# Patch-overlapping Go context after the fix

## `internal/controllers/admin/setting/adminSystemController.go` (changed lines (23, 84, 85, 86, 87, 88, 140, 141, 142, 143, 144, 145))

```go
/*
 * @Description:系统管理
 * @Author: gphper
 * @Date: 2021-06-01 20:15:04
 */

package setting

import (
	"bufio"
	"io/fs"
	"io/ioutil"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gphper/ginadmin/configs"
	"github.com/gphper/ginadmin/internal/controllers/admin"
	"github.com/gphper/ginadmin/internal/redis"
	"github.com/gphper/ginadmin/pkg/loggers"
	"github.com/gphper/ginadmin/pkg/utils/filesystem"
	gstrings "github.com/gphper/ginadmin/pkg/utils/strings"

	"github.com/gin-gonic/gin"
)

type adminSystemController struct {
	admin.BaseController
}

var Asc = adminSystemController{}

/**
日志目录页面
*/
func (con adminSystemController) Index(c *gin.Context) {

	var (
		path     string
		err      error
		log_path string
	)

	path = gstrings.JoinStr(configs.RootPath, string(filepath.Separator), "logs")

	files, err := ioutil.ReadDir(path)

	log_path = gstrings.JoinStr(string(filepath.Separator), "logs")

	if err != nil {
		loggers.LogError("admin", "读取目录失败", map[string]string{"error": err.Error()})
		con.ErrorHtml(c, err)
		return
	}

	c.HTML(http.StatusOK, "setting/systemlog.html", gin.H{
		"log_path": log_path,
		"files":    files,
		"line":     string(filepath.Separator),
	})
}

/**
获取目录
*/
func (con adminSystemController) GetDir(c *gin.Context) {

	type FileNode struct {
		Name string `json:"name"`
		Path string `json:"path"`
		Type string `json:"type"`
	}

	var (
		path      string
		err       error
		fileSlice []FileNode
		files     []fs.FileInfo
	)

	fileSlice = make([]FileNode, 0)
	path, err = filesystem.FilterPath(configs.RootPath+"logs", c.Query("path"))
	if err != nil {
		con.Error(c, err.Error())
		return
	}

	files, err = ioutil.ReadDir(path)
	if err != nil {
		con.Error(c, "获取目录失败")
		return
	}

	for _, v := range files {
		var fileType string
		if v.IsDir() {
			fileType = "dir"
		} else {
			fileType = "file"
		}
		fileSlice = append(fileSlice, FileNode{
			Name: v.Name(),
			Path: gstrings.JoinStr(c.Query("path"), string(filepath.Separator), v.Name()),
			Type: fileType,
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"data": fileSlice,
	})
}

/**
获取日志详情
*/
func (con adminSystemController) View(c *gin.Context) {

	var (
		err       error
		startLine int
		endLine   int
		scanner   *bufio.Scanner
		line      int
	)

	startLine, err = strconv.Atoi(c.DefaultQuery("start_line", "1"))
	if err != nil {
		con.ErrorHtml(c, err)
		return
	}
	endLine, err = strconv.Atoi(c.DefaultQuery("end_line", "20"))
	if err != nil {
		con.ErrorHtml(c, err)
		return
	}

	var filecontents []string
	filePath, err := filesystem.FilterPath(configs.RootPath+"logs", c.Query("path"))
	if err != nil {
		con.ErrorHtml(c, err)
		return
	}

	fi, err := os.Open(filePath)
	if err != nil {
		con.ErrorHtml(c, err)
		return
	}
	defer fi.Close()

	scanner = bufio.NewScanner(fi)
	for scanner.Scan() {
		line++
		if line >= startLine && line <= endLine {
			// 在要求行数内取得数据
			filecontents = append(filecontents, scanner.Text())
		} else {
			continue
		}
	}

	c.HTML(http.StatusOK, "setting/systemlog_view.html", gin.H{
		"file_path":    c.Query("path"),
		"filecontents": filecontents,
		"start_line":   startLine,
		"end_line":     endLine,
		"line":         line,
	})

}

/**
日志目录页面
*/
func (con adminSystemController) IndexRedis(c *gin.Context) {

	path := "logs"

	dateSlice, err := redis.RedisClient.Keys("logs:*").Result()

	if err != nil {
		loggers.LogError("admin", "读取目录失败", map[string]string{"error": err.Error()})
		con.ErrorHtml(c, err)
		return
	}

	dates := make(map[string]struct{})

	for _, v := range dateSlice {
		temp := strings.Split(v, ":")

		if _, ok := dates[temp[1]]; !ok {
			dates[temp[1]] = struct{}{}
		}
	}

	c.HTML(http.StatusOK, "setting/systemlog_redis.html", gin.H{
		"log_path": path,
		"files":    dates,
	})
}

/**
获取目录
*/
func (con adminSystemController) GetDirRedis(c *gin.Context) {

	path := c.Query("path")

	type FileNode struct {
		Name string `json:"name"`
		Path string `json:"path"`
		Type string `json:"type"`
	}

	pathSlice := strings.Split(path, "_")

	pattern := pathSlice[0] + ":*"

	dateSlice, err := redis.RedisClient.Keys(pattern).Result()

	if err != nil {
		loggers.LogError("admin", "读取目录失败", map[string]string{"error": err.Error()})
		con.ErrorHtml(c, err)
		return
	}

	fileSlice := make([]FileNode, 0)

	tempMap := make(map[string]struct{})

	for _, v := range dateSlice {
		temp := strings.Split(v, ":")
		index, _ := strconv.Atoi(pathSlice[1])
		var fileType string

		if index+2 == len(temp) {
			fileType = "file"
		} else {
			fileType = "dir"
		}

		if _, ok := tempMap[temp[index+1]]; ok {
			continue
		} else {
			tempMap[temp[index+1]] = struct{}{}
		}

		fileSlice = append(fileSlice, FileNode{
			Name: temp[index+1],
			Path: pathSlice[0] + ":" + temp[index+1] + "_" + strconv.Itoa(index+1),
			Type: fileType,
		})

	}

	c.JSON(http.StatusOK, gin.H{
		"data": fileSlice,
	})
}

/**
获取日志详情
*/
func (con adminSystemController) ViewRedis(c *gin.Context) {

	startLine, _ := strconv.Atoi(c.DefaultQuery("start_line", "1"))

	endLine, _ := strconv.Atoi(c.DefaultQuery("end_line", "20"))

	filePath := c.Query("path")

	pathSlice := strings.Split(filePath, "_")

	filecontents, _ := redis.RedisClient.LRange(pathSlice[0], int64(startLine-1), int64(endLine-1)).Result()

	line, _ := redis.RedisClient.LLen(pathSlice[0]).Result()

	c.HTML(http.StatusOK, "setting/systemlog_viewredis.html", gin.H{
		"file_path":    filePath,
		"filecontents": filecontents,
		"start_line":   startLine,
		"end_line":     endLine,
		"line":         line,
	})

}
```

## `pkg/utils/filesystem/filesystem.go` (changed lines (9, 10, 94, 96, 97, 98, 99, 100, 101, 103, 104, 105, 106, 107, 108, 109, 111, 112))

```go
/*
 * @Description:
 * @Author: gphper
 * @Date: 2022-03-27 10:57:10
 */
package filesystem

import (
	"errors"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"strings"
)

/**
获取项目根目录
*/
func RootPath() (path string, err error) {
	path = getCurrentAbPathByExecutable()
	if strings.Contains(path, getTmpDir()) {
		path = getCurrentAbPathByCaller()
	}
	path = strings.Replace(path, "pkg/utils/filesystem", "", 1)
	return
}

// 获取系统临时目录，兼容go run
func getTmpDir() string {
	dir := os.Getenv("TEMP")
	if dir == "" {
		dir = os.Getenv("TMP")
	}
	if dir == "" {
		dir = "tmp"
	}

	res, _ := filepath.EvalSymlinks(dir)
	return res
}

// 获取当前执行文件绝对路径
func getCurrentAbPathByExecutable() string {
	exePath, err := os.Executable()
	if err != nil {
		log.Fatal(err)
	}
	res, _ := filepath.EvalSymlinks(filepath.Dir(exePath))
	return res
}

// 获取当前执行文件绝对路径（go run）
func getCurrentAbPathByCaller() string {
	var abPath string
	_, filename, _, ok := runtime.Caller(0)
	if ok {
		abPath = path.Dir(filename)
	}
	return abPath
}

/**
* 打开文件句柄
**/
func OpenFile(filepath string) (file *os.File, err error) {

	file, err = os.OpenFile(filepath, os.O_WRONLY|os.O_CREATE, 0666)
	if err == nil {
		return
	}

	dir := path.Dir(filepath)
	_, err = os.Stat(dir)
	if err != nil {
		if os.IsNotExist(err) {
			err = os.MkdirAll(dir, fs.FileMode(os.O_CREATE))
			if err != nil {
				return
			}
		}
	}
	file, err = os.OpenFile(filepath, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return
	}
	return
}

/**
* 过滤非法访问的路径
 */
func FilterPath(root, path string) (string, error) {

	newPath := fmt.Sprintf("%s%s", root, path)
	absPath, err := filepath.Abs(newPath)
	if err != nil {
		return "", err
	}

	absPath = filepath.FromSlash(absPath)
	ifOver := filepath.HasPrefix(absPath, filepath.FromSlash(root))
	fmt.Println(absPath)
	fmt.Println(filepath.FromSlash(root))
	if !ifOver {
		return "", errors.New("access to the path is prohibited")
	}

	return absPath, nil
}
```
