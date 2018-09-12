using System.Collections;
using System.Collections.Generic;
using UnityEngine;

    static class func {

	public static float distance2  (Vector3 x, Vector3 y) {
        float res = 0;
        res = x.x * x.x + y.z * y.z;
        res = Mathf.Sqrt(res);
        return res; 
	}

    /// <summary>
    /// Получить ближайший вражеский полк
    /// </summary>
    /// <param name="reg_num">Номер построения относительно которого считаем</param>
    /// <param name="off_num">Номер офицера в списке офицеров полка, который принимает решение</param>
    /// <returns></returns>
    public static int getRegNear(int reg_num, int off_num)
    {
        /// Ближайший вражеский полк
        int num_nearest_enemy_reg = 99999;
        /// Считаем клетку текущего полка
        ///int cell_num = mainStatic.regiment_stat[reg_num].cellX + mainStatic.regiment_stat[reg_num].cellZ * mapCell.M;
        int cell_num = mainStatic.regiment_stat[reg_num].cell_number;
        float dist = 9999999;
        int r = 4;

        for (int i = -r; i < r; i++)
        {
            for (int j = -r; j < r; j++)
            {
                if (j == 0 && i == 0)
                {
                }
                else
                {
                    int temp = cell_num + i + j * mapCell.M;
                    //Debug.Log(temp);
                    for (int u = 0; u < mapCell.cell[temp].data.Count; u++)
                    {
                        //Debug.Log("New u");
                            if (mainStatic.regiment_stat[reg_num].command != mainStatic.regiment_stat[mapCell.cell[temp].data[u].regNum].command)
                            {
                                //Debug.Log("New command");
                                //Debug.Log(func.distance2(mainStatic.regiment_stat[reg_num].regPosition, mainStatic.regiment_stat[mapCell.cell[temp].data[u].regNum].regPosition));
                                if (func.distance2(mainStatic.regiment_stat[reg_num].regPosition, mainStatic.regiment_stat[mapCell.cell[temp].data[u].regNum].regPosition) < dist)
                                {
                                    //Debug.Log("New distance");
                                    dist = func.distance2(mainStatic.regiment_stat[reg_num].regPosition, mainStatic.regiment_stat[mapCell.cell[temp].data[u].regNum].regPosition);
                                    num_nearest_enemy_reg = mapCell.cell[temp].data[u].regNum;
                                }
                            }
                    }
                }
            }
        }
        return num_nearest_enemy_reg;
    }


    public static int getNearFromList(int reg_num, int off_num, int radius, int com_flag)
    {
        List <int> res = getListReg (reg_num, off_num, radius, com_flag);
        if (res.Count > 0)
        {
            // Если на таком радиусе есть враги, ищем ближайшее
            int temp = getRegNear(reg_num, off_num);
            return temp;
        }
        else
        {
            return 99999;
        }
    }

    /// <summary>
    /// Получить список  полков рядом
    /// </summary>
    /// <param name="reg_num">Номер полка относительно которого считаем</param>
    /// <param name="off_num">Номер офицера в контейнере офицеров в построении, потом можно будет убрать</param>
    /// <param name="my_count">Расстояние в клетках, на сколько в стороны будем смотреть</param>
    /// <param name="com_flag"> 1 - враги 2 - союзники 3 - все подряд </param>
    public static List<int> getListReg( int reg_num , int off_num , int radius, int com_flag)
    {
        /// Ближайший вражеский полк
        List <int> num_nearest_enemy_reg = new List<int>();
        /// Считаем клетку текущего полка
        int cell_num = mainStatic.regiment_stat[reg_num].cellX + mainStatic.regiment_stat[reg_num].cellZ * mapCell.M;
        float dist = 99999;
        for (int i = -radius; i < radius; i++)
        {
            for (int j = -radius; j < radius; j++)
            {
                if (j != 0 && i != 0)
                {
                    int temp = (cell_num + i + j * mapCell.M);
                    for (int u = 0; u < mapCell.cell[cell_num].data.Count; u++)
                    {
                        switch  (com_flag)
                        {
                            case (0):
                            if (mainStatic.regiment_stat[reg_num].command != mainStatic.regiment_stat[mapCell.cell[temp].data[u].command].command)
                            {
                                num_nearest_enemy_reg.Add(mapCell.cell[temp].data[u].regNum);
                            }
                            break;

                            case (1):
                            if (mainStatic.regiment_stat[reg_num].command == mainStatic.regiment_stat[mapCell.cell[temp].data[u].command].command)
                            {
                                num_nearest_enemy_reg.Add(mapCell.cell[temp].data[u].regNum);
                            }
                            break;

                            case (2):
                                num_nearest_enemy_reg.Add(mapCell.cell[temp].data[u].regNum);
                            break;
                        }
                    }
                }
            }
        }
        return num_nearest_enemy_reg;
    }

     /// <summary>
     /// Кладет в атакованный и защищающиеся полки информацию о начале атаки
     /// Отсюда вызвать всякие крики и все такое
     /// </summary>
     /// <param name="num_at"></param>
     /// <param name="num_def"></param>
    public static  void makeTwoRegementunderAttack(int num_at, int num_def)
    {
        mainStatic.regiment_stat[num_at].attack = num_def;
        mainStatic.regiment_stat[num_def].defen = num_at;
        //  Ответная реакция полка, и добавить условие, что есть командир
        if (mainStatic.regiment_stat[num_def].stateFighting == false)
        {
            //reaction.make_reaction(num_def);
        }
    }
}
